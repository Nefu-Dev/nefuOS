#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 随机森林实现
#include "randomforest.h"
#include <cmath>

namespace nefu {
namespace ml {

RandomForest::RandomForest() : n_trees(0), max_features(0), max_depth(6),
                               n_classes(0), d(0), mf_used(0), feat_sub(0), trees(0), regression(false) {}
RandomForest::~RandomForest() { free_forest(); }

void RandomForest::free_forest() {
    // 只释放堆数组，保留 n_trees / max_features 等配置（fit 会重设）
    if (trees) { delete[] trees; trees = 0; }
    if (feat_sub) { delete[] feat_sub; feat_sub = 0; }
}

void RandomForest::fit(const DataSet& ds, uint32_t seed) {
    free_forest();
    rng = Rng(seed);
    n_classes = ds.classes;
    d = ds.d;
    int mf = max_features > 0 ? max_features : ds.d;
    if (mf > ds.d) mf = ds.d;
    mf_used = mf;

    feat_sub = new int[(size_t)n_trees * mf];
    // 每棵树：随机选 mf 个特征（无放回）
    for (int t = 0; t < n_trees; t++) {
        int pool[64];
        for (int j = 0; j < ds.d; j++) pool[j] = j;
        for (int j = ds.d - 1; j > 0; j--) {
            int k = rng.next_int(j + 1);
            int tmp = pool[j]; pool[j] = pool[k]; pool[k] = tmp;
        }
        for (int j = 0; j < mf; j++) feat_sub[t * mf + j] = pool[j];
    }

    trees = new DTree[n_trees];
    for (int t = 0; t < n_trees; t++) {
        // bootstrap：n 个样本有放回抽取
        int* boot = new int[ds.n];
        for (int i = 0; i < ds.n; i++) boot[i] = rng.next_int(ds.n);

        // 投影：只取该树选中的特征列
        DataSet proj;
        proj.init(ds.n, mf, n_classes);
        for (int i = 0; i < ds.n; i++) {
            for (int j = 0; j < mf; j++)
                proj.row(i)[j] = ds.row(i)[feat_sub[t * mf + j]];
            proj.yc[i] = ds.yc[i];
        }
        trees[t].crit = CRIT_GINI;
        trees[t].max_depth = max_depth;
        trees[t].fit(proj, boot, ds.n);
        delete[] boot;
    }
}

int RandomForest::predict(const fix* x) const {
    int* votes = new int[n_classes];
    for (int c = 0; c < n_classes; c++) votes[c] = 0;
    for (int t = 0; t < n_trees; t++) {
        fix px[64];
        for (int j = 0; j < mf_used; j++) px[j] = x[feat_sub[t * mf_used + j]];
        votes[trees[t].predict(px)]++;
    }
    int best = 0;
    for (int c = 1; c < n_classes; c++) if (votes[c] > votes[best]) best = c;
    delete[] votes;
    return best;
}

fix RandomForest::accuracy(const DataSet& ds) const {
    int correct = 0;
    for (int i = 0; i < ds.n; i++) if (predict(ds.row(i)) == ds.yc[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(ds.n));
}

void RandomForest::fit_regression(const DataSet& ds, uint32_t seed) {
    free_forest();
    regression = true;
    rng = Rng(seed);
    d = ds.d;
    int mf = max_features > 0 ? max_features : ds.d;
    if (mf > ds.d) mf = ds.d;
    mf_used = mf;
    feat_sub = new int[(size_t)n_trees * mf];
    for (int t = 0; t < n_trees; t++) {
        int pool[64];
        for (int j = 0; j < ds.d; j++) pool[j] = j;
        for (int j = ds.d - 1; j > 0; j--) {
            int k = rng.next_int(j + 1);
            int tmp = pool[j]; pool[j] = pool[k]; pool[k] = tmp;
        }
        for (int j = 0; j < mf; j++) feat_sub[t * mf + j] = pool[j];
    }
    trees = new DTree[n_trees];
    for (int t = 0; t < n_trees; t++) {
        int* boot = new int[ds.n];
        for (int i = 0; i < ds.n; i++) boot[i] = rng.next_int(ds.n);
        DataSet proj;
        proj.init(ds.n, mf, 0);
        for (int i = 0; i < ds.n; i++) {
            for (int j = 0; j < mf; j++)
                proj.row(i)[j] = ds.row(i)[feat_sub[t * mf + j]];
            proj.yr[i] = ds.yr[i];
        }
        trees[t].max_depth = max_depth;
        trees[t].regression = true;
        trees[t].fit_regression(proj, boot, ds.n);
        delete[] boot;
    }
}

fix RandomForest::predict_regression(const fix* x) const {
    fix64 s = 0;
    for (int t = 0; t < n_trees; t++) {
        fix px[64];
        for (int j = 0; j < mf_used; j++) px[j] = x[feat_sub[t * mf_used + j]];
        s += (fix64)trees[t].predict_regression(px);
    }
    return fx::fx_div((fix)s, fx::itofix(n_trees));
}

fix RandomForest::mse(const DataSet& ds) const {
    fix64 s = 0;
    for (int i = 0; i < ds.n; i++) {
        fix e = predict_regression(ds.row(i)) - ds.yr[i];
        s += (fix64)e * e;
    }
    return fx::fx_div((fix)(s >> 16), fx::itofix(ds.n));
}

// ---------------- 自检 ----------------
int randomforest_self_test() {
    int fails = 0;

    // 两簇二维点：类0 在左下，类1 在右上
    DataSet ds;
    ds.init(12, 2, 2);
    fix pts[12][2] = {
        {-fx::itofix(3), -fx::itofix(3)},
        {-fx::itofix(2), -fx::itofix(4)},
        {-fx::itofix(4), -fx::itofix(2)},
        {-fx::itofix(3), -fx::itofix(2)},
        {-fx::itofix(2), -fx::itofix(3)},
        {-fx::itofix(4), -fx::itofix(3)},
        { fx::itofix(3),  fx::itofix(3)},
        { fx::itofix(4),  fx::itofix(2)},
        { fx::itofix(2),  fx::itofix(4)},
        { fx::itofix(3),  fx::itofix(4)},
        { fx::itofix(4),  fx::itofix(3)},
        { fx::itofix(2),  fx::itofix(3)},
    };
    int lbl[12];
    for (int i = 0; i < 6; i++) lbl[i] = 0;
    for (int i = 6; i < 12; i++) lbl[i] = 1;
    for (int i = 0; i < 12; i++) { ds.row(i)[0] = pts[i][0]; ds.row(i)[1] = pts[i][1]; ds.yc[i] = lbl[i]; }

    RandomForest rf;
    rf.n_trees = 10;
    rf.max_features = 2;
    rf.max_depth = 5;
    rf.fit(ds, 123);
    fix acc = rf.accuracy(ds);
    if (acc < fx::fxf(85, 100)) fails++;     // 训练集准确率 > 85%

    fix q0[2] = {-fx::itofix(2), -fx::itofix(2)};
    fix q1[2] = { fx::itofix(2),  fx::itofix(2)};
    if (rf.predict(q0) != 0) fails++;
    if (rf.predict(q1) != 1) fails++;
    // 回归森林：拟合一维阶跃 y = (x>0)?5:1
    {
        DataSet rd;
        rd.init(12, 1, 0);
        for (int i = 0; i < 6; i++) { rd.row(i)[0] = fx::itofix(-6+i); rd.yr[i] = fx::itofix(1); }
        for (int i = 6; i < 12; i++) { rd.row(i)[0] = fx::itofix(i); rd.yr[i] = fx::itofix(5); }
        RandomForest rf;
        rf.n_trees = 8; rf.max_features = 1; rf.max_depth = 4;
        rf.fit_regression(rd, 777);
        fix ql[1] = {fx::itofix(-4)};
        fix qh[1] = {fx::itofix(8)};
        fix pl = rf.predict_regression(ql);
        fix ph = rf.predict_regression(qh);
        if (!fx_close(pl, fx::FX_ONE*1, fx::fxf(50,100))) fails++;
        if (!fx_close(ph, fx::FX_ONE*5, fx::fxf(50,100))) fails++;
    }

    return fails;
}

} // namespace ml
} // namespace nefu
