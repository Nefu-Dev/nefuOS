#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 决策树分类实现（Q16.16）
#include "decisiontree.h"
#include <cmath>

namespace nefu {
namespace ml {

// ---------------- 节点 ----------------
DTreeNode::DTreeNode() : is_leaf(true), class_label(0), reg_value(0), feat(0), threshold(0),
                          left(0), right(0) {}
DTreeNode::~DTreeNode() { if (left) delete left; if (right) delete right; }

DTree::DTree() : max_depth(8), min_samples_split(2), crit(CRIT_GINI), regression(false), root(0) {}
DTree::~DTree() { free_tree(); }
void DTree::free_tree() { if (root) delete root; root = 0; }

// 节点不纯度：entropy 或 gini（Q16.16）
static fix node_impurity(const int* label_hist, int classes, int n, Criterion c) {
    if (n <= 0) return 0;
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(n));
    if (c == CRIT_GINI) {
        fix p2sum = 0;
        for (int k = 0; k < classes; k++) {
            if (label_hist[k] == 0) continue;
            fix p = fx::fx_mul(fx::itofix(label_hist[k]), invn);
            p2sum += fx::fx_mul(p, p);
        }
        return fx::FX_ONE - p2sum;
    }
    // entropy / gain ratio 都基于熵
    fix h = 0;
    for (int k = 0; k < classes; k++) {
        if (label_hist[k] == 0) continue;
        fix p = fx::fx_mul(fx::itofix(label_hist[k]), invn);
        fix lp = fx::fx_log2(p);        // <=0
        h -= fx::fx_mul(p, lp);          // -p*log2(p)
    }
    return h;
}

// 对一组样本统计类别直方图
static void hist_of(const DataSet& ds, const int* idx, int n_idx, int classes, int* hist) {
    for (int c = 0; c < classes; c++) hist[c] = 0;
    for (int i = 0; i < n_idx; i++) hist[ds.yc[idx[i]]]++;
}

static int majority(const int* hist, int classes) {
    int best = 0;
    for (int c = 1; c < classes; c++) if (hist[c] > hist[best]) best = c;
    return best;
}

// 求最佳分裂：遍历每个特征、每个候选阈值，返回最佳 (feat, thr) 与分裂后加权不纯度。
// 出参 best_feat / best_thr。返回加权子节点不纯度（越小越好）。
static fix best_split(const DataSet& ds, const int* idx, int n_idx, int classes,
                      Criterion crit, int* best_feat, fix* best_thr) {
    int* parent_hist = new int[classes];
    hist_of(ds, idx, n_idx, classes, parent_hist);
    fix parent_imp = node_impurity(parent_hist, classes, n_idx, crit);
    fix best_score = fx::FX_ONE;        // 越大越好（信息增益 / 增益率），初始 -inf 用反向
    // 这里我们直接比较加权子不纯度（越小越好）
    fix best_child_imp = parent_imp;     // 默认不分裂时等于父不纯度
    bool found = false;

    // 候选阈值：对每个特征，把 (value, label) 按 value 排序，取相邻中点
    // 为简化，用一个小规模数组（self_test 样本数很小）。
    fix* vals = new fix[n_idx];
    int* labs = new int[n_idx];
    int* left_hist = new int[classes];
    int* right_hist = new int[classes];

    for (int j = 0; j < ds.d; j++) {
        for (int i = 0; i < n_idx; i++) { vals[i] = ds.row(idx[i])[j]; labs[i] = ds.yc[idx[i]]; }
        // 插入排序（n 小）
        for (int i = 1; i < n_idx; i++) {
            fix v = vals[i]; int l = labs[i]; int k = i - 1;
            while (k >= 0 && vals[k] > v) { vals[k+1] = vals[k]; labs[k+1] = labs[k]; k--; }
            vals[k+1] = v; labs[k+1] = l;
        }
        // 逐个相邻中点尝试
        for (int s = 0; s < n_idx - 1; s++) {
            if (vals[s] == vals[s+1]) continue;       // 相等无法分裂
            fix thr = fx::fx_div(vals[s] + vals[s+1], fx::itofix(2));
            // 重新统计左右直方图（阈值 thr：<=thr 左）
            for (int c = 0; c < classes; c++) { left_hist[c] = 0; right_hist[c] = 0; }
            int nl = 0, nr = 0;
            for (int i = 0; i < n_idx; i++) {
                if (ds.row(idx[i])[j] <= thr) { left_hist[labs[i]]++; nl++; }
                else { right_hist[labs[i]]++; nr++; }
            }
            if (nl == 0 || nr == 0) continue;
            fix il = node_impurity(left_hist, classes, nl, crit);
            fix ir = node_impurity(right_hist, classes, nr, crit);
            fix wimp = fx::fx_mul(fx::fx_div(fx::itofix(nl), fx::itofix(n_idx)), il)
                     + fx::fx_mul(fx::fx_div(fx::itofix(nr), fx::itofix(n_idx)), ir);
            // 增益 = parent_imp - wimp
            if (wimp < best_child_imp) {
                best_child_imp = wimp;
                *best_feat = j; *best_thr = thr;
                found = true;
            }
        }
    }
    (void)parent_imp; (void)best_score;
    delete[] parent_hist; delete[] vals; delete[] labs;
    delete[] left_hist; delete[] right_hist;
    return found ? best_child_imp : parent_imp;
}

static DTreeNode* build(const DataSet& ds, int* idx, int n_idx, int classes,
                        int depth, int max_depth, int min_split, Criterion crit) {
    int* hist = new int[classes];
    hist_of(ds, idx, n_idx, classes, hist);
    int lab = majority(hist, classes);
    // 纯度判定：只有一个非零类
    int nz = 0;
    for (int c = 0; c < classes; c++) if (hist[c] > 0) nz++;
    bool pure = (nz <= 1);
    delete[] hist;

    DTreeNode* node = new DTreeNode();
    if (pure || depth >= max_depth || n_idx < min_split) {
        node->is_leaf = true;
        node->class_label = lab;
        return node;
    }

    int best_feat = 0; fix best_thr = 0;
    best_split(ds, idx, n_idx, classes, crit, &best_feat, &best_thr);

    // 划分左右
    int* lidx = new int[n_idx];
    int* ridx = new int[n_idx];
    int nl = 0, nr = 0;
    for (int i = 0; i < n_idx; i++) {
        if (ds.row(idx[i])[best_feat] <= best_thr) lidx[nl++] = idx[i];
        else ridx[nr++] = idx[i];
    }
    if (nl == 0 || nr == 0) {   // 退化：当叶子
        delete[] lidx; delete[] ridx;
        node->is_leaf = true; node->class_label = lab;
        return node;
    }
    node->is_leaf = false;
    node->feat = best_feat;
    node->threshold = best_thr;
    node->left = build(ds, lidx, nl, classes, depth + 1, max_depth, min_split, crit);
    node->right = build(ds, ridx, nr, classes, depth + 1, max_depth, min_split, crit);
    delete[] lidx; delete[] ridx;
    return node;
}

void DTree::fit(const DataSet& ds, const int* idx, int n_idx) {
    free_tree();
    int* buf = new int[n_idx];
    for (int i = 0; i < n_idx; i++) buf[i] = idx[i];
    root = build(ds, buf, n_idx, ds.classes, 0, max_depth, min_samples_split, crit);
    delete[] buf;
}

static int predict_node(const DTreeNode* node, const fix* x) {
    if (node->is_leaf) return node->class_label;
    if (x[node->feat] <= node->threshold) return predict_node(node->left, x);
    return predict_node(node->right, x);
}

int DTree::predict(const fix* x) const {
    if (!root) return 0;
    return predict_node(root, x);
}

fix DTree::accuracy(const DataSet& ds) const {
    int correct = 0;
    for (int i = 0; i < ds.n; i++) if (predict(ds.row(i)) == ds.yc[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(ds.n));
}

// ==================== 回归树（CART，MSE 分裂） ====================
// 节点回归值：该节点所有样本 y 的均值
static fix reg_mean(const DataSet& ds, const int* idx, int n_idx) {
    fix64 s = 0;
    for (int i = 0; i < n_idx; i++) s += (fix64)ds.yr[idx[i]];
    return fx::fx_div((fix)s, fx::itofix(n_idx));
}

// 节点加权 MSE（方差 * n）：用于选择最佳分裂
static fix best_split_reg(const DataSet& ds, const int* idx, int n_idx,
                          int* best_feat, fix* best_thr) {
    fix* vals = new fix[n_idx];
    fix* ty   = new fix[n_idx];
    fix best_reduction = 0;
    bool found = false;

    for (int j = 0; j < ds.d; j++) {
        for (int i = 0; i < n_idx; i++) { vals[i] = ds.row(idx[i])[j]; ty[i] = ds.yr[idx[i]]; }
        // 插入排序
        for (int i = 1; i < n_idx; i++) {
            fix v = vals[i]; fix t = ty[i]; int k = i - 1;
            while (k >= 0 && vals[k] > v) { vals[k+1] = vals[k]; ty[k+1] = ty[k]; k--; }
            vals[k+1] = v; ty[k+1] = t;
        }
        // 前缀和：便于快速算左右均值与平方误差
        fix64* prevs = new fix64[n_idx + 1];
        prevs[0] = 0;
        for (int i = 0; i < n_idx; i++) prevs[i+1] = prevs[i] + (fix64)ty[i];
        fix64 total = prevs[n_idx];
        for (int s = 0; s < n_idx - 1; s++) {
            if (vals[s] == vals[s+1]) continue;
            int nl = s + 1, nr = n_idx - nl;
            fix ml = fx::fx_div((fix)prevs[nl], fx::itofix(nl));
            fix mr = fx::fx_div((fix)(total - prevs[nl]), fx::itofix(nr));
            // 加权方差 reduction = nl*(ml)^2 + nr*(mr)^2 - (total)^2/n
            fix64 lm = (fix64)ml * (fix64)ml;
            fix64 rm = (fix64)mr * (fix64)mr;
            fix64 red = (fix64)nl * lm + (fix64)nr * rm - (fix64)total * (fix64)total / n_idx;
            red >>= 16;
            if (red > best_reduction) {
                best_reduction = (fix)red;
                *best_feat = j;
                *best_thr = fx::fx_div(vals[s] + vals[s+1], fx::itofix(2));
                found = true;
            }
        }
        delete[] prevs;
    }
    delete[] vals; delete[] ty;
    return found ? best_reduction : 0;
}

static DTreeNode* build_reg(const DataSet& ds, int* idx, int n_idx,
                           int depth, int max_depth, int min_split) {
    fix mv = reg_mean(ds, idx, n_idx);
    DTreeNode* node = new DTreeNode();
    if (depth >= max_depth || n_idx < min_split) {
        node->is_leaf = true; node->reg_value = mv;
        return node;
    }
    int bf = 0; fix bt = 0;
    fix red = best_split_reg(ds, idx, n_idx, &bf, &bt);
    if (red <= 0) {
        node->is_leaf = true; node->reg_value = mv;
        return node;
    }
    int* lidx = new int[n_idx];
    int* ridx = new int[n_idx];
    int nl = 0, nr = 0;
    for (int i = 0; i < n_idx; i++) {
        if (ds.row(idx[i])[bf] <= bt) lidx[nl++] = idx[i];
        else ridx[nr++] = idx[i];
    }
    if (nl == 0 || nr == 0) {
        delete[] lidx; delete[] ridx;
        node->is_leaf = true; node->reg_value = mv;
        return node;
    }
    node->is_leaf = false; node->feat = bf; node->threshold = bt;
    node->left  = build_reg(ds, lidx, nl, depth + 1, max_depth, min_split);
    node->right = build_reg(ds, ridx, nr, depth + 1, max_depth, min_split);
    delete[] lidx; delete[] ridx;
    return node;
}

void DTree::fit_regression(const DataSet& ds, const int* idx, int n_idx) {
    free_tree();
    regression = true;
    int* buf = new int[n_idx];
    for (int i = 0; i < n_idx; i++) buf[i] = idx[i];
    root = build_reg(ds, buf, n_idx, 0, max_depth, min_samples_split);
    delete[] buf;
}

static fix predict_reg_node(const DTreeNode* node, const fix* x) {
    if (node->is_leaf) return node->reg_value;
    if (x[node->feat] <= node->threshold) return predict_reg_node(node->left, x);
    return predict_reg_node(node->right, x);
}

fix DTree::predict_regression(const fix* x) const {
    if (!root) return 0;
    return predict_reg_node(root, x);
}

fix DTree::mse(const DataSet& ds) const {
    fix64 s = 0;
    for (int i = 0; i < ds.n; i++) {
        fix e = predict_regression(ds.row(i)) - ds.yr[i];
        s += (fix64)e * e;
    }
    return fx::fx_div((fix)(s >> 16), fx::itofix(ds.n));
}

// ---------------- 自检 ----------------
int decisiontree_self_test() {
    int fails = 0;

    // 二维数据：x<0 为类0，x>=0 为类1（y 噪声）
    // 类0: (-2,-1),(-1,-2),(-3,1)；类1: (2,1),(1,2),(3,-1)
    DataSet ds;
    ds.init(6, 2, 2);
    fix data[6][2] = {
        {-fx::itofix(2), -fx::itofix(1)},
        {-fx::itofix(1), -fx::itofix(2)},
        {-fx::itofix(3),  fx::itofix(1)},
        { fx::itofix(2),  fx::itofix(1)},
        { fx::itofix(1),  fx::itofix(2)},
        { fx::itofix(3), -fx::itofix(1)},
    };
    int lbl[6] = {0,0,0,1,1,1};
    for (int i = 0; i < 6; i++) {
        ds.row(i)[0] = data[i][0]; ds.row(i)[1] = data[i][1]; ds.yc[i] = lbl[i];
    }
    int all[6] = {0,1,2,3,4,5};

    // 三种准则各跑一遍，训练集准确率应 100%（线性可分）
    Criterion crits[3] = {CRIT_GINI, CRIT_ENTROPY, CRIT_GAINRATIO};
    for (int c = 0; c < 3; c++) {
        DTree t;
        t.crit = crits[c];
        t.max_depth = 4;
        t.fit(ds, all, 6);
        fix acc = t.accuracy(ds);
        if (acc < fx::FX_ONE - fx::fxf(5, 100)) fails++;   // 至少 95%
    }

    // 预测新点：(-1,5) 应类0，(1,5) 应类1
    {
        DTree t;
        t.crit = CRIT_GINI; t.max_depth = 4;
        t.fit(ds, all, 6);
        fix q0[2] = {-fx::itofix(1), fx::itofix(5)};
        fix q1[2] = { fx::itofix(1), fx::itofix(5)};
        if (t.predict(q0) != 0) fails++;
        if (t.predict(q1) != 1) fails++;
    }
    // 回归树：拟合阶跃函数 x<0 时 y=1，x>=0 时 y=5
    {
        DataSet rd;
        rd.init(6, 1, 1);
        fix rx[6] = {-3,-2,-1, 1,2,3};
        fix ry[6] = {1,1,1, 5,5,5};
        for (int i = 0; i < 6; i++) { rd.row(i)[0] = fx::itofix(rx[i]); rd.yr[i] = fx::itofix(ry[i]); }
        int ridx[6] = {0,1,2,3,4,5};
        DTree rt; rt.max_depth = 3;
        rt.fit_regression(rd, ridx, 6);
        fix ql[1] = {fx::itofix(-2)};
        fix qr[1] = {fx::itofix(2)};
        fix pl = rt.predict_regression(ql);
        fix pr = rt.predict_regression(qr);
        if (!fx_close(pl, fx::FX_ONE * 1, fx::fxf(20,100))) fails++;
        if (!fx_close(pr, fx::FX_ONE * 5, fx::fxf(20,100))) fails++;
    }

    return fails;
}

} // namespace ml
} // namespace nefu
