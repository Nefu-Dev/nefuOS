#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 线性模型实现（Q16.16）
#include "linear.h"
#include <cmath>

namespace nefu {
namespace ml {

// ==================== 线性回归 ====================
LinearReg::LinearReg() : d(0), w(0), b(0) {}
LinearReg::~LinearReg() { release(); }
void LinearReg::release() { if (w) delete[] w; w = 0; d = 0; b = 0; }
void LinearReg::init(int d_) { release(); d = d_; w = new fix[d]; for (int j = 0; j < d; j++) w[j] = 0; b = 0; }

fix LinearReg::predict(const fix* x) const {
    fix64 s = 0;
    for (int j = 0; j < d; j++) s += (fix64)w[j] * (fix64)x[j];
    return (fix)(s >> 16) + b;
}

void LinearReg::fit_gd(const fix* Xflat, const fix* y, int n, int d_,
                       fix lr, int iters, fix l2) {
    init(d_);
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(n));
    for (int it = 0; it < iters; it++) {
        fix64* gw = new fix64[d];
        for (int j = 0; j < d; j++) gw[j] = 0;
        fix64 gb = 0;
        for (int i = 0; i < n; i++) {
            const fix* x = Xflat + (size_t)i * d_;
            fix pred = predict(x);
            fix err = pred - y[i];
            for (int j = 0; j < d; j++) gw[j] += (fix64)err * (fix64)x[j];
            gb += (fix64)err;
        }
        for (int j = 0; j < d; j++) {
            fix g = (fix)(gw[j] >> 16);
            g = fx::fx_mul(g, invn);
            // L2 正则：grad += l2 * w
            g += fx::fx_mul(l2, w[j]);
            w[j] -= fx::fx_mul(lr, g);
        }
        // gb = Σ err（err 本身就是 Q16.16，不是乘积，不能再 >>16）；
        // 直接除以样本数得到平均偏置梯度。
        fix gb_mean = (fix)(gb / (fix64)n);
        b -= fx::fx_mul(lr, gb_mean);
        delete[] gw;
    }
}

void LinearReg::fit_sgd(const fix* Xflat, const fix* y, int n, int d_,
                        fix lr, int epochs, uint32_t seed) {
    init(d_);
    Rng rng(seed);
    int* order = new int[n];
    for (int e = 0; e < epochs; e++) {
        for (int i = 0; i < n; i++) order[i] = i;
        for (int i = n - 1; i > 0; i--) {
            int j = rng.next_int(i + 1);
            int t = order[i]; order[i] = order[j]; order[j] = t;
        }
        for (int k = 0; k < n; k++) {
            int i = order[k];
            const fix* x = Xflat + (size_t)i * d_;
            fix pred = predict(x);
            fix err = pred - y[i];
            for (int j = 0; j < d; j++)
                w[j] -= fx::fx_mul(lr, fx::fx_mul(err, x[j]));
            b -= fx::fx_mul(lr, err);
        }
    }
    delete[] order;
}

bool LinearReg::fit_normal_eq(const fix* Xflat, const fix* y, int n, int d_) {
    init(d_);
    // 构造增广 Xa [n x (d+1)]，最后一列全 1（偏置）
    int D = d_ + 1;
    FxMat Xa(n, D);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < d_; j++) Xa(i, j) = Xflat[(size_t)i * d_ + j];
        Xa(i, d_) = fx::FX_ONE;
    }
    FxMat Xt = mat_transpose(Xa);
    FxMat XtX = mat_mul(Xt, Xa);
    // XtX w = Xt y
    fix* Xty = new fix[D];
    for (int j = 0; j < D; j++) {
        fix64 s = 0;
        for (int i = 0; i < n; i++) s += (fix64)Xt(j, i) * (fix64)y[i];
        Xty[j] = (fix)(s >> 16);
    }
    fix* sol = mat_solve(XtX, Xty);
    delete[] Xty;
    if (!sol) return false;
    for (int j = 0; j < d_; j++) w[j] = sol[j];
    b = sol[d_];
    delete[] sol;
    return true;
}

fix LinearReg::mse(const fix* Xflat, const fix* y, int n) const {
    fix64 s = 0;
    for (int i = 0; i < n; i++) {
        const fix* x = Xflat + (size_t)i * d;
        fix e = predict(x) - y[i];
        s += (fix64)e * e;
    }
    fix v = (fix)(s >> 16);
    return fx::fx_div(v, fx::itofix(n));
}

fix LinearReg::r2_score(const fix* Xflat, const fix* y, int n) const {
    // 均值
    fix64 sy = 0;
    for (int i = 0; i < n; i++) sy += (fix64)y[i];
    fix ybar = (fix)(sy / n);
    fix64 sst = 0, sse = 0;
    for (int i = 0; i < n; i++) {
        const fix* x = Xflat + (size_t)i * d;
        fix e = predict(x) - y[i];
        sse += (fix64)e * e;
        fix t = y[i] - ybar;
        sst += (fix64)t * t;
    }
    if (sst == 0) return 0;
    fix ssef = (fix)(sse >> 16);
    fix sstf = (fix)(sst >> 16);
    return fx::FX_ONE - fx::fx_div(ssef, sstf);
}

fix LinearReg::mae(const fix* Xflat, const fix* y, int n) const {
    fix64 s = 0;
    for (int i = 0; i < n; i++) {
        const fix* x = Xflat + (size_t)i * d;
        fix e = predict(x) - y[i];
        s += (fix64)(e < 0 ? -e : e);
    }
    return fx::fx_div((fix)s, fx::itofix(n));
}

bool LinearReg::fit_ridge(const fix* Xflat, const fix* y, int n, int d_, fix l2) {
    release(); d = d_; w = new fix[d]; for (int j = 0; j < d; j++) w[j] = 0; b = 0;
    // 构造增广矩阵 Xa (n x (d+1))，最后一列为 1（偏置）
    int D = d + 1;
    FxMat Xa(n, D);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < d; j++) Xa(i, j) = Xflat[(size_t)i * d + j];
        Xa(i, d) = fx::FX_ONE;
    }
    FxMat Xt = mat_transpose(Xa);
    FxMat XtX = mat_mul(Xt, Xa);          // (d+1)x(d+1)
    // L2 加到权重对角（不加偏置列，即最后一维不惩罚）
    for (int j = 0; j < d; j++) XtX(j, j) += l2;
    // Xt y
    FxMat yv(n, 1);
    for (int i = 0; i < n; i++) yv(i, 0) = y[i];
    FxMat Xty = mat_mul(Xt, yv);          // (d+1)x1
    // 解 (XtX) theta = Xty
    fix* rhs = new fix[D];
    for (int j = 0; j < D; j++) rhs[j] = Xty(j, 0);
    fix* theta = mat_solve(XtX, rhs);
    delete[] rhs;
    if (!theta) { release(); return false; }
    for (int j = 0; j < d; j++) w[j] = theta[j];
    b = theta[d];
    delete[] theta;
    return true;
}

// ==================== 逻辑回归 ====================
LogisticReg::LogisticReg() : d(0), w(0), b(0) {}
LogisticReg::~LogisticReg() { release(); }
void LogisticReg::release() { if (w) delete[] w; w = 0; d = 0; b = 0; }
void LogisticReg::init(int d_) { release(); d = d_; w = new fix[d]; for (int j = 0; j < d; j++) w[j] = 0; b = 0; }

fix LogisticReg::predict_prob(const fix* x) const {
    fix64 s = 0;
    for (int j = 0; j < d; j++) s += (fix64)w[j] * (fix64)x[j];
    fix z = (fix)(s >> 16) + b;
    return sigmoid(z);
}

int LogisticReg::predict_class(const fix* x) const {
    return predict_prob(x) >= fx::FX_HALF ? 1 : 0;
}

void LogisticReg::fit_gd(const fix* Xflat, const int* y, int n, int d_,
                         fix lr, int iters, fix l2) {
    init(d_);
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(n));
    for (int it = 0; it < iters; it++) {
        fix64* gw = new fix64[d];
        for (int j = 0; j < d; j++) gw[j] = 0;
        fix64 gb = 0;
        for (int i = 0; i < n; i++) {
            const fix* x = Xflat + (size_t)i * d_;
            fix p = predict_prob(x);
            fix err = p - fx::itofix(y[i]);     // p - y
            for (int j = 0; j < d; j++) gw[j] += (fix64)err * (fix64)x[j];
            gb += (fix64)err;
        }
        for (int j = 0; j < d; j++) {
            fix g = fx::fx_mul((fix)(gw[j] >> 16), invn);
            g += fx::fx_mul(l2, w[j]);
            w[j] -= fx::fx_mul(lr, g);
        }
        fix gb_mean = (fix)(gb / (fix64)n);
        b -= fx::fx_mul(lr, gb_mean);
        delete[] gw;
    }
}

fix LogisticReg::accuracy(const fix* Xflat, const int* y, int n) const {
    int correct = 0;
    for (int i = 0; i < n; i++)
        if (predict_class(Xflat + (size_t)i * d) == y[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(n));
}

// ==================== OvR 多分类 ====================
LogisticOvR::LogisticOvR() : classes(0), d(0), models(0) {}
LogisticOvR::~LogisticOvR() { release(); }
void LogisticOvR::release() {
    if (models) { delete[] models; models = 0; }
    classes = 0; d = 0;
}
void LogisticOvR::init(int classes_, int d_) {
    release();
    classes = classes_; d = d_;
    models = new LogisticReg[classes];
    for (int c = 0; c < classes; c++) models[c].init(d_);
}

void LogisticOvR::fit(const fix* Xflat, const int* y, int n, fix lr, int iters) {
    int* binary = new int[n];
    for (int c = 0; c < classes; c++) {
        for (int i = 0; i < n; i++) binary[i] = (y[i] == c) ? 1 : 0;
        models[c].fit_gd(Xflat, binary, n, d, lr, iters);
    }
    delete[] binary;
}

int LogisticOvR::predict(const fix* x) const {
    int best = 0;
    fix bp = models[0].predict_prob(x);
    for (int c = 1; c < classes; c++) {
        fix p = models[c].predict_prob(x);
        if (p > bp) { bp = p; best = c; }
    }
    return best;
}

// ==================== 自检 ====================
int linear_self_test() {
    int fails = 0;
    const fix TOL = fx::fxf(5, 100);        // 0.05 容差

    // --- 线性回归：y = 2x + 1，单变量 5 点 ---
    // x = 0,1,2,3,4  ->  y = 1,3,5,7,9
    const int N = 5;
    fix X[5], y[5];
    for (int i = 0; i < N; i++) { X[i] = fx::itofix(i); y[i] = fx::itofix(2 * i + 1); }

    // 1. 正规方程：应得 w=2, b=1
    LinearReg lr;
    if (!lr.fit_normal_eq(X, y, N, 1)) fails++;
    else {
        if (!fx_close(lr.w[0], fx::FX_TWO, TOL)) fails++;
        if (!fx_close(lr.b, fx::FX_ONE, TOL)) fails++;
    }

    // 2. 批量梯度下降：应收敛到 w≈2, b≈1
    LinearReg gd;
    gd.fit_gd(X, y, N, 1, fx::fxf(1, 10), 3000);
    if (!fx_close(gd.w[0], fx::FX_TWO, TOL)) fails++;
    if (!fx_close(gd.b, fx::FX_ONE, TOL)) fails++;
    // MSE 应很小
    if (gd.mse(X, y, N) > fx::fxf(1, 100)) fails++;

    // 3. SGD
    LinearReg sg;
    sg.fit_sgd(X, y, N, 1, fx::fxf(1, 100), 200, 7);
    if (!fx_close(sg.w[0], fx::FX_TWO, TOL)) fails++;
    if (!fx_close(sg.b, fx::FX_ONE, TOL)) fails++;

    // --- 逻辑回归：线性可分两类 ---
    // 类0：x<0 一侧 (-1,-2,-3)；类1：x>0 一侧 (1,2,3)
    const int M = 6;
    fix lX[6]; int ly[6];
    lX[0] = fx::itofix(-1); lX[1] = fx::itofix(-2); lX[2] = fx::itofix(-3); ly[0]=ly[1]=ly[2]=0;
    lX[3] = fx::itofix(1);  lX[4] = fx::itofix(2);  lX[5] = fx::itofix(3);  ly[3]=ly[4]=ly[5]=1;
    LogisticReg lg;
    lg.fit_gd(lX, ly, M, 1, fx::fxf(5, 10), 2000);
    fix acc = lg.accuracy(lX, ly, M);
    if (acc < fx::fxf(95, 100)) fails++;     // 训练集准确率 > 95%

    // --- OvR：三簇单变量数据 ---
    // 类0：-5,-4；类1：0,1；类2：4,5
    fix oX[6]; int oy[6];
    oX[0]=fx::itofix(-5); oX[1]=fx::itofix(-4); oy[0]=oy[1]=0;
    oX[2]=fx::itofix(0);  oX[3]=fx::itofix(1);   oy[2]=oy[3]=1;
    oX[4]=fx::itofix(4);  oX[5]=fx::itofix(5);   oy[4]=oy[5]=2;
    LogisticOvR ovr;
    ovr.init(3, 1);
    ovr.fit(oX, oy, 6, fx::fxf(2, 10), 2000);
    int ok = 0;
    for (int i = 0; i < 6; i++) if (ovr.predict(&oX[i]) == oy[i]) ok++;
    if (ok < 5) fails++;
    // R²/MAE/Ridge：y=2x+1 上 R² 应≈1，MAE≈0
    {
        fix X[5] = {fx::itofix(0), fx::itofix(1), fx::itofix(2), fx::itofix(3), fx::itofix(4)};
        fix yy[5];
        for (int i = 0; i < 5; i++) yy[i] = fx::itofix(2 * i + 1);
        LinearReg lr;
        lr.fit_normal_eq(X, yy, 5, 1);
        fix r2 = lr.r2_score(X, yy, 5);
        if (r2 < fx::fxf(95, 100)) fails++;       // R² > 0.95
        fix emae = lr.mae(X, yy, 5);
        if (emae > fx::fxf(5, 100)) fails++;      // MAE < 0.05
        // Ridge 也应能拟合（λ 很小）
        LinearReg rg;
        if (!rg.fit_ridge(X, yy, 5, 1, fx::fxf(1, 100))) fails++;
        if (!fx_close(rg.w[0], fx::FX_ONE * 2, fx::fxf(10,100))) fails++;
    }
    // Softmax 三分类：x<0 类0，0<=x<3 类1，x>=3 类2
    {
        fix X[9] = {-5,-3,-1, 1,2,3, 4,5,6};
        int y[9] = {0,0,0, 1,1,1, 2,2,2};
        SoftmaxReg sm;
        sm.init(1, 3);
        sm.fit(X, y, 9, fx::fxf(1,10), 5000);
        fix acc = sm.accuracy(X, y, 9);
        if (acc < fx::fxf(55,100)) fails++;
    }

    return fails;
}
// ==================== Softmax 多分类逻辑回归 ====================
SoftmaxReg::SoftmaxReg() : d(0), classes(0), W(0), B(0) {}
SoftmaxReg::~SoftmaxReg() { release(); }
void SoftmaxReg::release() {
    if (W) delete[] W; W = 0;
    if (B) delete[] B; B = 0;
    d = 0; classes = 0;
}
void SoftmaxReg::init(int d_, int classes_) {
    release();
    d = d_; classes = classes_;
    W = new fix[(size_t)classes * d];
    B = new fix[classes];
    for (int i = 0; i < classes * d; i++) W[i] = 0;
    for (int i = 0; i < classes; i++) B[i] = 0;
}

void SoftmaxReg::predict_proba(const fix* x, fix* out) const {
    // 计算 logits，然后 softmax
    fix* logits = new fix[classes];
    for (int c = 0; c < classes; c++) {
        fix64 s = (fix64)B[c];
        for (int j = 0; j < d; j++)
            s += (fix64)W[(size_t)c * d + j] * (fix64)x[j];
        logits[c] = (fix)(s >> 16);
    }
    fix mx = logits[0];
    for (int c = 1; c < classes; c++) if (logits[c] > mx) mx = logits[c];
    fix64 sum = 0;
    for (int c = 0; c < classes; c++) {
        fix e = fx::fx_exp(logits[c] - mx);
        out[c] = e; sum += (fix64)e;
    }
    fix inv = fx::fx_div(fx::FX_ONE, (fix)sum);
    for (int c = 0; c < classes; c++) out[c] = fx::fx_mul(out[c], inv);
    delete[] logits;
}

int SoftmaxReg::predict(const fix* x) const {
    fix* p = new fix[classes];
    predict_proba(x, p);
    int best = 0; fix bv = p[0];
    for (int c = 1; c < classes; c++) if (p[c] > bv) { bv = p[c]; best = c; }
    delete[] p;
    return best;
}

void SoftmaxReg::fit(const fix* Xflat, const int* yc, int n, fix lr, int iters) {
    fix* proba = new fix[classes];
    for (int it = 0; it < iters; it++) {
        for (int s = 0; s < n; s++) {
            const fix* x = Xflat + (size_t)s * d;
            predict_proba(x, proba);
            // 梯度：对每个类 c，grad = (p[c] - 1{y==c}) * x
            for (int c = 0; c < classes; c++) {
                fix err = proba[c] - (yc[s] == c ? fx::FX_ONE : 0);
                B[c] -= fx::fx_mul(lr, err);
                for (int j = 0; j < d; j++)
                    W[(size_t)c * d + j] -= fx::fx_mul(lr, fx::fx_mul(err, x[j]));
            }
        }
    }
    delete[] proba;
}

fix SoftmaxReg::accuracy(const fix* Xflat, const int* yc, int n) const {
    int correct = 0;
    for (int i = 0; i < n; i++)
        if (predict(Xflat + (size_t)i * d) == yc[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(n));
}

// 单变量多项式展开
void poly_expand_univariate(fix x, int degree, fix* out) {
    out[0] = fx::FX_ONE;
    for (int i = 1; i < degree; i++) out[i] = fx::fx_mul(out[i-1], x);
}

} // namespace ml
} // namespace nefu
