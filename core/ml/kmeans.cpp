#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— K-Means 聚类实现（Q16.16）
#include "kmeans.h"
#include <cmath>

namespace nefu {
namespace ml {

KMeans::KMeans() : k(0), d(0), centers(0), labels(0), max_iter(50) {}
KMeans::~KMeans() { free_centers(); }
void KMeans::free_centers() {
    if (centers) delete[] centers;
    if (labels) delete[] labels;
    centers = 0; labels = 0; k = 0; d = 0;
}

void KMeans::fit(const fix* Xflat, int n, int d_, int k_, uint32_t seed) {
    free_centers();
    k = k_; d = d_;
    centers = new fix[(size_t)k * d];
    labels = new int[n];
    Rng rng(seed);

    // ---- K-Means++ 初始化 ----
    // 第一个中心随机选一个样本
    int first = rng.next_int(n);
    for (int j = 0; j < d; j++) centers[j] = Xflat[(size_t)first * d + j];
    fix* min_d2 = new fix[n];     // 每点到最近中心的距离平方
    for (int i = 0; i < n; i++) min_d2[i] = dist2_euclid(Xflat + (size_t)i * d, centers, d);

    for (int c = 1; c < k; c++) {
        // 累计权重 Σ min_d2[i]
        fix64 total = 0;
        for (int i = 0; i < n; i++) total += (fix64)min_d2[i];
        if (total <= 0) {
            // 所有点已聚到已有中心，新中心随便选一个未选过的
            for (int j = 0; j < d; j++) centers[(size_t)c * d + j] = Xflat[(size_t)((c % n)) * d + j];
            continue;
        }
        // 按权重随机选一个点作为下一个中心
        uint32_t target = rng.next_u32() % (uint32_t)(total >> 16);
        fix64 cum = 0;
        int chosen = 0;
        for (int i = 0; i < n; i++) {
            cum += (fix64)min_d2[i];
            if ((uint32_t)(cum >> 16) >= target) { chosen = i; break; }
        }
        for (int j = 0; j < d; j++) centers[(size_t)c * d + j] = Xflat[(size_t)chosen * d + j];
        // 更新每点到最近中心的距离
        for (int i = 0; i < n; i++) {
            fix dd = dist2_euclid(Xflat + (size_t)i * d, centers + (size_t)c * d, d);
            if (dd < min_d2[i]) min_d2[i] = dd;
        }
    }
    delete[] min_d2;

    // ---- EM 迭代 ----
    int* counts = new int[k];
    for (int iter = 0; iter < max_iter; iter++) {
        // 分配
        bool changed = false;
        for (int i = 0; i < n; i++) {
            const fix* x = Xflat + (size_t)i * d;
            int best = 0;
            fix bd = dist2_euclid(x, centers, d);
            for (int c = 1; c < k; c++) {
                fix dd = dist2_euclid(x, centers + (size_t)c * d, d);
                if (dd < bd) { bd = dd; best = c; }
            }
            if (labels[i] != best) { labels[i] = best; changed = true; }
        }
        if (!changed) break;
        // 更新质心
        for (int c = 0; c < k; c++) counts[c] = 0;
        for (int j = 0; j < d; j++)
            for (int c = 0; c < k; c++) centers[(size_t)c * d + j] = 0;
        for (int i = 0; i < n; i++) {
            int c = labels[i];
            counts[c]++;
            for (int j = 0; j < d; j++) centers[(size_t)c * d + j] += Xflat[(size_t)i * d + j];
        }
        for (int c = 0; c < k; c++) {
            if (counts[c] == 0) continue;
            for (int j = 0; j < d; j++)
                centers[(size_t)c * d + j] = fx::fx_div(centers[(size_t)c * d + j], fx::itofix(counts[c]));
        }
    }
    delete[] counts;
}

int KMeans::predict(const fix* x) const {
    int best = 0;
    fix bd = dist2_euclid(x, centers, d);
    for (int c = 1; c < k; c++) {
        fix dd = dist2_euclid(x, centers + (size_t)c * d, d);
        if (dd < bd) { bd = dd; best = c; }
    }
    return best;
}

fix KMeans::inertia(const fix* Xflat, int n) const {
    fix64 s = 0;
    for (int i = 0; i < n; i++) {
        int c = labels ? labels[i] : predict(Xflat + (size_t)i * d);
        // dist2_euclid 已返回 Q16.16，直接累加（不要再 >>16）
        s += (fix64)dist2_euclid(Xflat + (size_t)i * d, centers + (size_t)c * d, d);
    }
    return (fix)s;
}

fix KMeans::silhouette(const fix* Xflat, int n) const {
    fix64 ssum = 0;
    int valid = 0;
    for (int i = 0; i < n; i++) {
        const fix* xi = Xflat + (size_t)i * d;
        int ci = labels[i];
        // a(i)：同簇平均距离
        fix64 sa = 0; int na = 0;
        // b(i)：到最近其他簇的平均距离
        fix bb = fx::FX_ONE * 1000;   // 大数
        for (int c = 0; c < k; c++) {
            fix64 sc = 0; int nc = 0;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                if (labels[j] != c) continue;
                sc += (fix64)dist_euclid(xi, Xflat + (size_t)j * d, d);
                nc++;
            }
            if (c == ci) { sa = sc; na = nc; }
            else if (nc > 0) {
                fix mc = fx::fx_div((fix)(sc >> 16), fx::itofix(nc));
                if (mc < bb) bb = mc;
            }
        }
        if (na == 0) continue;
        fix a = fx::fx_div((fix)(sa >> 16), fx::itofix(na));
        fix mx = a > bb ? a : bb;
        if (mx <= 0) continue;
        fix s_i = fx::fx_div(bb - a, mx);
        ssum += (fix64)s_i;
        valid++;
    }
    if (valid == 0) return 0;
    return fx::fx_div((fix)ssum, fx::itofix(valid));
}

// 多重启：保留 inertia 最小的模型
void KMeans::fit_best(const fix* Xflat, int n, int d_, int k_, int restarts, uint32_t seed) {
    fix best_inertia = 0;
    fix* best_centers = 0;
    int* best_labels = 0;
    for (int r = 0; r < restarts; r++) {
        fit(Xflat, n, d_, k_, seed + (uint32_t)r * 7919u);
        fix in = inertia(Xflat, n);
        if (r == 0 || in < best_inertia) {
            best_inertia = in;
            if (!best_centers) best_centers = new fix[(size_t)k_ * d_];
            for (int i = 0; i < k_ * d_; i++) best_centers[i] = centers[i];
            if (!best_labels) best_labels = new int[n];
            for (int i = 0; i < n; i++) best_labels[i] = labels[i];
        }
    }
    // 把最优结果写回
    for (int i = 0; i < k * d; i++) centers[i] = best_centers[i];
    for (int i = 0; i < n; i++) labels[i] = best_labels[i];
    delete[] best_centers;
    delete[] best_labels;
}

// Davies-Bouldin 指数：簇内平均距离 / 簇心距离，取各簇最相似者平均
fix KMeans::davies_bouldin(const fix* Xflat, int n) const {
    if (k <= 1) return 0;
    // 每簇样本数与簇内平均半径
    int* cnt = new int[k];
    fix* sumd = new fix[k];
    for (int c = 0; c < k; c++) { cnt[c] = 0; sumd[c] = 0; }
    for (int i = 0; i < n; i++) {
        int c = labels[i];
        cnt[c]++;
        sumd[c] += dist_euclid(Xflat + (size_t)i * d, centers + (size_t)c * d, d);
    }
    fix* avg = new fix[k];
    for (int c = 0; c < k; c++)
        avg[c] = cnt[c] > 0 ? fx::fx_div(sumd[c], fx::itofix(cnt[c])) : 0;
    // 对每簇找 max_{j!=i} (avg_i+avg_j)/dist(ci,cj)
    fix64 db = 0;
    for (int i = 0; i < k; i++) {
        fix worst = 0;
        for (int j = 0; j < k; j++) {
            if (j == i) continue;
            fix dcc = dist_euclid(centers + (size_t)i * d, centers + (size_t)j * d, d);
            if (dcc == 0) dcc = 1;
            fix r = fx::fx_div(avg[i] + avg[j], dcc);
            if (r > worst) worst = r;
        }
        db += (fix64)worst;
    }
    delete[] cnt; delete[] sumd; delete[] avg;
    return fx::fx_div((fix)db, fx::itofix(k));
}

// ==================== K-Medoids ====================
KMedoids::KMedoids() : k(0), d(0), medoids(0), labels(0) {}
KMedoids::~KMedoids() {
    if (medoids) delete[] medoids;
    if (labels) delete[] labels;
    medoids = 0; labels = 0;
}

void KMedoids::fit(const fix* Xflat, int n, int d_, int k_, int iters, uint32_t seed) {
    k = k_; d = d_;
    if (medoids) delete[] medoids;
    if (labels) delete[] labels;
    medoids = new int[k];
    labels = new int[n];
    Rng rng(seed);
    // 初始化：随机选 k 个不同样本作为 medoid
    int* pool = new int[n];
    for (int i = 0; i < n; i++) pool[i] = i;
    for (int i = n - 1; i > 0; i--) {
        int j = rng.next_int(i + 1);
        int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
    }
    for (int c = 0; c < k; c++) medoids[c] = pool[c];
    delete[] pool;

    for (int it = 0; it < iters; it++) {
        // 分配：每个样本归到最近 medoid
        bool changed = false;
        for (int i = 0; i < n; i++) {
            fix best = 0; int bestc = 0;
            for (int c = 0; c < k; c++) {
                fix dd = dist_euclid(Xflat + (size_t)i * d,
                                     Xflat + (size_t)medoids[c] * d, d);
                if (c == 0 || dd < best) { best = dd; bestc = c; }
            }
            if (labels[i] != bestc) { labels[i] = bestc; changed = true; }
        }
        // 更新：每簇选使簇内绝对距离和最小的样本作 medoid
        for (int c = 0; c < k; c++) {
            fix best_cost = 0; int best_idx = -1;
            for (int cand = 0; cand < n; cand++) {
                if (labels[cand] != c) continue;
                fix64 cost = 0;
                for (int i = 0; i < n; i++)
                    if (labels[i] == c)
                        cost += (fix64)dist_euclid(Xflat + (size_t)i * d,
                                                   Xflat + (size_t)cand * d, d);
                fix cf = (fix)cost;
                if (best_idx < 0 || cf < best_cost) { best_cost = cf; best_idx = cand; }
            }
            if (best_idx >= 0) medoids[c] = best_idx;
        }
        if (!changed) break;
    }
}

fix KMedoids::total_cost(const fix* Xflat, int n) const {
    fix64 s = 0;
    for (int i = 0; i < n; i++)
        s += (fix64)dist_euclid(Xflat + (size_t)i * d,
                                Xflat + (size_t)medoids[labels[i]] * d, d);
    return (fix)s;
}

int kmedoids_self_test() {
    int fails = 0;
    // 两个明显簇
    fix X[8][2] = {
        {fx::itofix(0),fx::itofix(0)},{fx::itofix(1),fx::itofix(0)},
        {fx::itofix(0),fx::itofix(1)},{fx::itofix(1),fx::itofix(1)},
        {fx::itofix(10),fx::itofix(10)},{fx::itofix(11),fx::itofix(10)},
        {fx::itofix(10),fx::itofix(11)},{fx::itofix(11),fx::itofix(11)},
    };
    fix flat[16];
    for (int i = 0; i < 8; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }
    KMedoids km;
    km.fit(flat, 8, 2, 2, 50, 42);
    // medoids 应分别在两簇内
    if (km.medoids[0] >= 4 && km.medoids[1] >= 4) fails++;  // 同簇
    if (km.medoids[0] < 4 && km.medoids[1] < 4) fails++;
    // 标签应把前4个和后4个分开
    if (km.labels[0] != km.labels[3]) fails++;
    if (km.labels[4] != km.labels[7]) fails++;
    if (km.labels[0] == km.labels[4]) fails++;
    return fails;
}

// ---------------- 自检 ----------------
int kmeans_self_test() {
    int fails = 0;

    // 两个明显分离的二维簇：
    // 簇A: (0,0),(1,0),(0,1),(1,1)；簇B: (10,10),(11,10),(10,11),(11,11)
    fix X[8][2] = {
        {fx::itofix(0), fx::itofix(0)},
        {fx::itofix(1), fx::itofix(0)},
        {fx::itofix(0), fx::itofix(1)},
        {fx::itofix(1), fx::itofix(1)},
        {fx::itofix(10), fx::itofix(10)},
        {fx::itofix(11), fx::itofix(10)},
        {fx::itofix(10), fx::itofix(11)},
        {fx::itofix(11), fx::itofix(11)},
    };
    fix flat[16];
    for (int i = 0; i < 8; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }

    KMeans km;
    km.fit(flat, 8, 2, 2, 99);

    // 前 4 个点应同簇，后 4 个点应另一簇
    int c0 = km.labels[0];
    for (int i = 1; i < 4; i++) if (km.labels[i] != c0) fails++;
    int c1 = km.labels[4];
    for (int i = 5; i < 8; i++) if (km.labels[i] != c1) fails++;
    if (c0 == c1) fails++;      // 两个簇必须不同

    // 簇心应接近 (0.5,0.5) 和 (10.5,10.5)
    // 找到对应左下的簇心
    fix* lo = km.centers;
    fix* hi = km.centers + 2;
    if (lo[0] > hi[0]) { fix* t = lo; lo = hi; hi = t; }
    if (!fx_close(lo[0], fx::fxf(1, 2), fx::fxf(2, 10))) fails++;
    if (!fx_close(lo[1], fx::fxf(1, 2), fx::fxf(2, 10))) fails++;
    if (!fx_close(hi[0], fx::itofix(10) + fx::fxf(1, 2), fx::fxf(2, 10))) fails++;
    if (!fx_close(hi[1], fx::itofix(10) + fx::fxf(1, 2), fx::fxf(2, 10))) fails++;

    // inertia 应较小（簇内紧凑）
    fix in = km.inertia(flat, 8);
    if (in > fx::FX_ONE * 4) fails++;     // 每点离簇心很近

    // silhouette 应为正（簇分离良好）
    fix sil = km.silhouette(flat, 8);
    if (sil < 0) fails++;

    return fails;
}

} // namespace ml
} // namespace nefu
