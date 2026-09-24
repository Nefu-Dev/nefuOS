#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— K 近邻实现
#include "knn.h"

namespace nefu {
namespace ml {

KnnModel::KnnModel() : n(0), d(0), X(0), yc(0), metric(DIST_EUCLIDEAN), weighted(false) {}

void KnnModel::bind(const fix* X_, const int* Xc_, int n_, int d_) {
    X = X_; yc = Xc_; n = n_; d = d_;
}

fix KnnModel::distance(const fix* a, const fix* b) const {
    switch (metric) {
    case DIST_MANHATTAN: return dist_manhattan(a, b, d);
    case DIST_COSINE: {
        // 余弦距离 = 1 - (a·b)/(|a||b|)
        fix dprod = dot(a, b, d);
        fix na = l2norm(a, d);
        fix nb = l2norm(b, d);
        if (na == 0 || nb == 0) return fx::FX_ONE;
        fix cos = fx::fx_div(dprod, fx::fx_mul(na, nb));
        return fx::FX_ONE - cos;        // 距离 [0,2]
    }
    case DIST_EUCLIDEAN:
    default: return dist_euclid(a, b, d);
    }
}

// 在已有 best 列表（长度 <=k，按距离升序）中尝试插入 (idx, dist)。
// best_idx / best_d 为出参数组（长度 k）。返回当前列表长度。
static void knn_try_insert(int* best_idx, fix* best_d, int& kcur, int k, int idx, fix dist) {
    if (kcur < k) {
        // 直接插入并冒泡到正确位置
        int pos = kcur;
        best_idx[pos] = idx; best_d[pos] = dist;
        while (pos > 0 && best_d[pos] < best_d[pos - 1]) {
            fix td = best_d[pos]; best_d[pos] = best_d[pos-1]; best_d[pos-1] = td;
            int ti = best_idx[pos]; best_idx[pos] = best_idx[pos-1]; best_idx[pos-1] = ti;
            pos--;
        }
        kcur++;
    } else if (dist < best_d[k - 1]) {
        // 替换最远元素，下沉
        int pos = k - 1;
        best_idx[pos] = idx; best_d[pos] = dist;
        while (pos > 0 && best_d[pos] < best_d[pos - 1]) {
            fix td = best_d[pos]; best_d[pos] = best_d[pos-1]; best_d[pos-1] = td;
            int ti = best_idx[pos]; best_idx[pos] = best_idx[pos-1]; best_idx[pos-1] = ti;
            pos--;
        }
    }
}

int KnnModel::classify(const fix* x, int k, int classes) const {
    if (k > n) k = n;
    int* best_idx = new int[k];
    fix* best_d = new fix[k];
    int kcur = 0;
    for (int i = 0; i < n; i++) {
        fix dd = distance(x, X + (size_t)i * d);
        knn_try_insert(best_idx, best_d, kcur, k, i, dd);
    }
    // 加权或等权投票
    fix* votes = new fix[classes];
    for (int c = 0; c < classes; c++) votes[c] = 0;
    for (int t = 0; t < kcur; t++) {
        int label = yc[best_idx[t]];
        fix w = fx::FX_ONE;
        if (weighted) {
            // 权重 = 1/(d+eps)，用倒数近似
            fix inv = fx::fx_div(fx::FX_ONE, best_d[t] + fx::fxf(1, 100));
            w = inv;
        }
        votes[label] += w;
    }
    int best = 0; fix bv = votes[0];
    for (int c = 1; c < classes; c++)
        if (votes[c] > bv) { bv = votes[c]; best = c; }
    delete[] best_idx; delete[] best_d; delete[] votes;
    return best;
}

fix KnnModel::regress(const fix* x, int k, const fix* yr) const {
    if (k > n) k = n;
    int* best_idx = new int[k];
    fix* best_d = new fix[k];
    int kcur = 0;
    for (int i = 0; i < n; i++) {
        fix dd = distance(x, X + (size_t)i * d);
        knn_try_insert(best_idx, best_d, kcur, k, i, dd);
    }
    fix64 s = 0;
    for (int t = 0; t < kcur; t++) s += (fix64)yr[best_idx[t]];   // Q16.16 求和，不位移
    delete[] best_idx; delete[] best_d;
    if (kcur <= 0) return 0;
    return fx::fx_div((fix)s, fx::itofix(kcur));
}

fix KnnModel::accuracy(const fix* Xt, const int* yt, int nt, int k, int classes) const {
    int correct = 0;
    for (int i = 0; i < nt; i++)
        if (classify(Xt + (size_t)i * d, k, classes) == yt[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(nt));
}

// ---------------- 自检 ----------------
int knn_self_test() {
    int fails = 0;

    // 两类二维点：类0 在左下方，类1 在右上方
    // 类0: (-3,-3),(-2,-4),(-4,-2)；类1: (3,3),(4,2),(2,4)
    const fix P[6][2] = {
        {-fx::itofix(3), -fx::itofix(3)},
        {-fx::itofix(2), -fx::itofix(4)},
        {-fx::itofix(4), -fx::itofix(2)},
        { fx::itofix(3),  fx::itofix(3)},
        { fx::itofix(4),  fx::itofix(2)},
        { fx::itofix(2),  fx::itofix(4)},
    };
    int labels[6] = {0,0,0,1,1,1};
    fix flat[12];
    for (int i = 0; i < 6; i++) { flat[i*2] = P[i][0]; flat[i*2+1] = P[i][1]; }

    KnnModel m;
    m.bind(flat, labels, 6, 2);
    m.metric = DIST_EUCLIDEAN;

    // 测试点 (-2,-3) 应分类为 0
    fix q1[2] = {-fx::itofix(2), -fx::itofix(3)};
    if (m.classify(q1, 3, 2) != 0) fails++;
    // 测试点 (3,3) 应分类为 1
    fix q2[2] = {fx::itofix(3), fx::itofix(3)};
    if (m.classify(q2, 3, 2) != 1) fails++;

    // 曼哈顿度量也应正确
    m.metric = DIST_MANHATTAN;
    if (m.classify(q1, 3, 2) != 0) fails++;
    if (m.classify(q2, 3, 2) != 1) fails++;

    // 回归：yr = [0,0,0,10,10,10]，查询 (3,3) 应返回接近 10
    fix yr[6] = {0,0,0, fx::itofix(10),fx::itofix(10),fx::itofix(10)};
    m.metric = DIST_EUCLIDEAN;
    fix pred = m.regress(q2, 3, yr);
    if (!fx_close(pred, fx::itofix(10), fx::fxf(1, 10))) fails++;

    // 加权投票开关不崩溃
    m.weighted = true;
    if (m.classify(q1, 3, 2) != 0) fails++;
    m.weighted = false;

    return fails;
}

} // namespace ml
} // namespace nefu
