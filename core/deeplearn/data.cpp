// nefuOS 深度学习库 —— 微型数据集工具实现
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "data.h"

namespace nefu {
namespace deeplearn {

Tensor DataSet::sample_x(int i) const {
    int F = X.size / N;
    Tensor r = t_zeros(2, (int[2]){1, F});
    for (int j = 0; j < F; j++) r.data[j] = X.data[i*F+j];
    return r;
}

DataSet make_regression(const fix* xflat, const fix* yflat, int N, int F) {
    DataSet ds;
    ds.N = N;
    ds.X = t_from_flat(2, (int[2]){N, F}, xflat);
    ds.Y = t_from_flat(2, (int[2]){N, 1}, yflat);
    return ds;
}

Tensor batch_x(const DataSet& ds, int start, int batch) {
    int F = ds.X.size / ds.N;
    int b = batch;
    if (start + b > ds.N) b = ds.N - start;
    Tensor r = t_zeros(2, (int[2]){b, F});
    for (int i = 0; i < b; i++)
        for (int j = 0; j < F; j++)
            r.data[i*F+j] = ds.X.data[(start+i)*F+j];
    return r;
}

Tensor batch_y(const DataSet& ds, int start, int batch) {
    int b = batch;
    if (start + b > ds.N) b = ds.N - start;
    Tensor r = t_zeros(2, (int[2]){b, 1});
    for (int i = 0; i < b; i++) r.data[i] = ds.Y.data[start+i];
    return r;
}

void shuffle_idx(int* idx, int N, uint32_t seed) {
    // Fisher-Yates，用 LCG 产生随机下标
    uint32_t s = seed ? seed : 1u;
    for (int i = 0; i < N; i++) idx[i] = i;
    for (int i = N - 1; i > 0; i--) {
        s = s * 1103515245u + 12345u;
        int j = (int)(s % (uint32_t)(i + 1));
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
}

Tensor normalize_columns(const Tensor& x, fix* out_mean, fix* out_var, int F) {
    int N = x.size / F;
    Tensor r = t_zeros(x.nd, x.shape);
    for (int j = 0; j < F; j++) {
        fix64 s = 0;
        for (int i = 0; i < N; i++) s += (fix64)x.data[i*F+j];
        fix mean = fx::fx_div((fix)s, fx::itofix(N));
        fix64 v = 0;
        for (int i = 0; i < N; i++) { fix d = x.data[i*F+j]-mean; v += (fix64)d*d; }
        fix var = fx::fx_div((fix)v, fx::itofix(N));
        if (out_mean) out_mean[j] = mean;
        if (out_var) out_var[j] = var;
        fix inv = fx::fx_div(fx::FX_ONE, fx::fx_sqrt(var + fx::fxf(1,10000)));
        for (int i = 0; i < N; i++)
            r.data[i*F+j] = fx::fx_mul(x.data[i*F+j]-mean, inv);
    }
    return r;
}

// ---------------- 自检 ----------------
int data_self_test() {
    int fails = 0;
    fix tol = fx::fxf(2,100);
    // make_regression + batch
    {
        fix x[6] = {0, fx::FX_ONE, fx::itofix(2), fx::itofix(3), fx::itofix(4), fx::itofix(5)};
        fix y[3] = {0, fx::FX_ONE, fx::itofix(2)};
        DataSet ds = make_regression(x, y, 3, 2);
        Tensor bx = batch_x(ds, 0, 2);
        if (bx.shape[0]!=2 || bx.shape[1]!=2) fails++;
        if (!fx_close(bx.data[2], fx::itofix(2), tol)) fails++;
        Tensor by = batch_y(ds, 1, 2);
        if (!fx_close(by.data[0], fx::FX_ONE, tol)) fails++;
    }
    // shuffle：长度 N 的排列
    {
        int idx[5] = {0,0,0,0,0};
        shuffle_idx(idx, 5, 1234);
        int seen[5] = {0,0,0,0,0};
        for (int i=0;i<5;i++){ if(idx[i]<0||idx[i]>=5){fails++;} else seen[idx[i]]++; }
        for (int i=0;i<5;i++) if (seen[i]!=1) fails++;
    }
    // normalize：常数列归一后近似 0
    {
        fix v[4] = {fx::itofix(3),fx::itofix(3),fx::itofix(3),fx::itofix(3)};
        Tensor X = t_from_flat(2,(int[2]){2,2},v);
        fix m[2], var[2];
        Tensor Z = normalize_columns(X, m, var, 2);
        // 常数列方差~0，归一输出~0
        if (!fx_close(Z.data[0], 0, fx::fxf(5,100))) fails++;
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
