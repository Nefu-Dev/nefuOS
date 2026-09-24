#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 数据集管理实现
#include "dataset.h"
#include <cmath>

namespace nefu {
namespace ml {

// ---------------- 生命周期 ----------------
DataSet::DataSet() : n(0), d(0), classes(0), X(0), yc(0), yr(0) {}

DataSet::~DataSet() { release(); }

void DataSet::release() {
    if (X) delete[] X;
    if (yc) delete[] yc;
    if (yr) delete[] yr;
    X = 0; yc = 0; yr = 0; n = 0; d = 0; classes = 0;
}

void DataSet::init(int n_, int d_, int classes_) {
    release();
    n = n_; d = d_; classes = classes_;
    if (n > 0 && d > 0) X = new fix[(size_t)n * d];
    if (n > 0) yc = new int[n];
    if (n > 0) yr = new fix[n];
}

// ---------------- 训练/测试划分 ----------------
void train_test_split(const DataSet& ds, fix test_frac, uint32_t seed,
                      int* train_idx, int* test_idx, int* n_train, int* n_test) {
    int n = ds.n;
    // 先生成 0..n-1 的随机排列（Fisher-Yates）
    int* perm = new int[n];
    for (int i = 0; i < n; i++) perm[i] = i;
    Rng rng(seed);
    for (int i = n - 1; i > 0; i--) {
        int j = rng.next_int(i + 1);
        int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    // 测试集大小 = round(n * test_frac)
    int nt = (int)((fx::fix64)n * (fix64)test_frac >> 16);
    if (nt < 0) nt = 0;
    if (nt > n) nt = n;
    int tr = n - nt;
    for (int i = 0; i < tr; i++) train_idx[i] = perm[i];
    for (int i = 0; i < nt; i++) test_idx[i] = perm[tr + i];
    delete[] perm;
    *n_train = tr;
    *n_test = nt;
}

// K 折交叉验证索引：洗牌后依次把样本分配到 0..k-1 折
void kfold_indices(int n, int k, uint32_t seed, int* folds) {
    int* perm = new int[n];
    for (int i = 0; i < n; i++) perm[i] = i;
    Rng rng(seed);
    for (int i = n - 1; i > 0; i--) {
        int j = rng.next_int(i + 1);
        int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (int i = 0; i < n; i++) folds[perm[i]] = i % k;
    delete[] perm;
}
// ---------------- 归一化参数 ----------------
NormParams::NormParams() : d(0), mn(0), mx(0), mean(0), std(0) {}
NormParams::~NormParams() { release(); }
void NormParams::alloc(int d_) {
    release();
    d = d_;
    if (d > 0) {
        mn = new fix[d]; mx = new fix[d];
        mean = new fix[d]; std = new fix[d];
    }
}
void NormParams::release() {
    if (mn) delete[] mn;
    if (mx) delete[] mx;
    if (mean) delete[] mean;
    if (std) delete[] std;
    mn = mx = mean = std = 0; d = 0;
}

void norm_minmax_fit(const DataSet& ds, const int* idx, int n_idx, NormParams* p) {
    int d = ds.d;
    p->alloc(d);
    for (int j = 0; j < d; j++) {
        fix mn = ds.row(idx[0])[j];
        fix mx = mn;
        for (int i = 1; i < n_idx; i++) {
            fix v = ds.row(idx[i])[j];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        p->mn[j] = mn;
        p->mx[j] = mx;
    }
}

void norm_minmax_apply(const NormParams& p, fix* row_d) {
    for (int j = 0; j < p.d; j++) {
        fix rng = p.mx[j] - p.mn[j];
        if (rng <= 0) { row_d[j] = 0; continue; }      // 常量维映射到 0
        row_d[j] = fx::fx_div(row_d[j] - p.mn[j], rng); // [0,1]
    }
}

void norm_zscore_fit(const DataSet& ds, const int* idx, int n_idx, NormParams* p) {
    int d = ds.d;
    p->alloc(d);
    for (int j = 0; j < d; j++) {
        fix64 s = 0;
        for (int i = 0; i < n_idx; i++) s += (fix64)ds.row(idx[i])[j];
        // s 是 Q16.16 值之和（不是乘积），直接除以 n 得 Q16.16 均值，不能 >>16
        fix mean = (fix)(s / (fix64)n_idx);
        // 方差 = E[(x-mean)^2]
        fix64 vs = 0;
        for (int i = 0; i < n_idx; i++) {
            fix diff = ds.row(idx[i])[j] - mean;
            vs += (fix64)diff * diff;
        }
        fix var = fx::fx_div((fix)(vs >> 16), fx::itofix(n_idx));
        fix sd = var > 0 ? fx::fx_sqrt(var) : fx::FX_ONE;
        if (sd < fx::fxf(1, 100)) sd = fx::FX_ONE;     // 钳位，避免除零
        p->mean[j] = mean;
        p->std[j] = sd;
    }
}

void norm_zscore_apply(const NormParams& p, fix* row_d) {
    for (int j = 0; j < p.d; j++)
        row_d[j] = fx::fx_div(row_d[j] - p.mean[j], p.std[j]);
}

// ---------------- one-hot ----------------
void one_hot(int c, int classes, fix* out) {
    for (int i = 0; i < classes; i++) out[i] = (i == c) ? fx::FX_ONE : 0;
}

int one_hot_argmax(const fix* v, int classes) {
    int best = 0;
    fix bv = v[0];
    for (int i = 1; i < classes; i++)
        if (v[i] > bv) { bv = v[i]; best = i; }
    return best;
}

// ---------------- 类别统计 ----------------
void class_counts(const DataSet& ds, int* count, int classes) {
    for (int c = 0; c < classes; c++) count[c] = 0;
    for (int i = 0; i < ds.n; i++) count[ds.yc[i]]++;
}

int num_classes_present(const DataSet& ds) {
    if (ds.classes <= 0) return 0;
    int* cnt = new int[ds.classes];
    for (int c = 0; c < ds.classes; c++) cnt[c] = 0;
    for (int i = 0; i < ds.n; i++) if (ds.yc[i] >= 0 && ds.yc[i] < ds.classes) cnt[ds.yc[i]]++;
    int k = 0;
    for (int c = 0; c < ds.classes; c++) if (cnt[c] > 0) k++;
    delete[] cnt;
    return k;
}

// ---------------- 自检 ----------------
int dataset_self_test() {
    int fails = 0;

    // 构造 6 样本 2 维数据集
    DataSet ds;
    ds.init(6, 2, 2);
    // X: [0,0],[1,10],[2,20],[3,30],[4,40],[5,50]
    for (int i = 0; i < 6; i++) {
        ds.row(i)[0] = fx::itofix(i);
        ds.row(i)[1] = fx::itofix(i * 10);
        ds.yc[i] = i % 2;
        ds.yr[i] = fx::itofix(2 * i + 1);
    }

    // 1. one_hot(2,3) = [0,0,1]
    fix oh[3];
    one_hot(2, 3, oh);
    if (oh[0] != 0 || oh[1] != 0 || oh[2] != fx::FX_ONE) fails++;
    if (one_hot_argmax(oh, 3) != 2) fails++;

    // 2. min-max 归一化：第一维范围 [0,5]，映射后应在 [0,1]
    int idx[6] = {0, 1, 2, 3, 4, 5};
    NormParams p;
    norm_minmax_fit(ds, idx, 6, &p);
    fix row0[2]; for (int j = 0; j < 2; j++) row0[j] = ds.row(0)[j];
    fix row5[2]; for (int j = 0; j < 2; j++) row5[j] = ds.row(5)[j];
    norm_minmax_apply(p, row0);
    norm_minmax_apply(p, row5);
    // 最小值应变 0，最大值应变 1
    if (!fx_close(row0[0], 0, fx::fxf(1, 100))) fails++;
    if (!fx_close(row5[0], fx::FX_ONE, fx::fxf(1, 100))) fails++;

    // 3. 训练/测试划分：80% 测试 -> 测试约 5 个，训练约 1 个，总数仍 6
    int tr[6], te[6];
    int ntr = 0, nte = 0;
    train_test_split(ds, fx::fxf(80, 100), 42, tr, te, &ntr, &nte);
    if (ntr + nte != 6) fails++;
    if (ntr < 1 || nte < 1) fails++;

    // 4. 类别计数：两类各 3
    int cc[2];
    class_counts(ds, cc, 2);
    if (cc[0] != 3 || cc[1] != 3) fails++;

    // 5. z-score：对常数维不应除零崩溃（构造一个全 7 的第三维）
    DataSet ds2;
    ds2.init(4, 1, 0);
    for (int i = 0; i < 4; i++) { ds2.row(i)[0] = fx::itofix(7); ds2.yc[i] = 0; }
    int idx2[4] = {0, 1, 2, 3};
    NormParams p2;
    norm_zscore_fit(ds2, idx2, 4, &p2);
    fix cst[1] = {fx::itofix(7)};
    norm_zscore_apply(p2, cst);     // 不应除零崩溃；结果应为 0（mean=7）
    if (!fx_close(cst[0], 0, fx::fxf(1, 100))) fails++;

    return fails;
}

} // namespace ml
} // namespace nefu
