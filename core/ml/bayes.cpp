#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 朴素贝叶斯实现（Q16.16）
#include "bayes.h"
#include <cmath>

namespace nefu {
namespace ml {

// ==================== 高斯 NB ====================
GaussianNB::GaussianNB() : classes(0), d(0), mean(0), var(0), logprior(0) {}
GaussianNB::~GaussianNB() { release(); }
void GaussianNB::release() {
    if (mean) delete[] mean;
    if (var) delete[] var;
    if (logprior) delete[] logprior;
    mean = var = 0; logprior = 0; classes = 0; d = 0;
}

void GaussianNB::fit(const fix* Xflat, const int* yc, int n) {
    release();
    // 统计类别数
    int mx = 0;
    for (int i = 0; i < n; i++) if (yc[i] > mx) mx = yc[i];
    classes = mx + 1;
    d = 0;
    // 维数由调用方隐含：这里用 0，调用后由 predict 长度决定；
    // 为简单，要求调用方先设 d。约定：fit 前已设置 d。
    if (d <= 0) d = 2;   // 兜底
    mean = new fix[(size_t)classes * d];
    var = new fix[(size_t)classes * d];
    logprior = new fix[classes];

    int* cnt = new int[classes];
    for (int c = 0; c < classes; c++) cnt[c] = 0;
    for (int i = 0; i < n; i++) cnt[yc[i]]++;

    for (int c = 0; c < classes; c++) {
        logprior[c] = fx::fx_ln(fx::fx_div(fx::itofix(cnt[c]), fx::itofix(n)));
        for (int j = 0; j < d; j++) {
            fix64 s = 0;
            int nc = 0;
            for (int i = 0; i < n; i++) if (yc[i] == c) { s += (fix64)Xflat[(size_t)i*d + j]; nc++; }
            fix mu = nc > 0 ? (fix)(s / (fix64)nc) : 0;
            fix64 vs = 0;
            for (int i = 0; i < n; i++) if (yc[i] == c) {
                fix diff = Xflat[(size_t)i*d + j] - mu;
                vs += (fix64)diff * diff;
            }
            fix v = nc > 0 ? fx::fx_div((fix)(vs >> 16), fx::itofix(nc)) : fx::FX_ONE;
            if (v < fx::fxf(1, 100)) v = fx::fxf(1, 100);   // 方差下限
            mean[(size_t)c*d + j] = mu;
            var[(size_t)c*d + j] = v;
        }
    }
    delete[] cnt;
}

int GaussianNB::predict(const fix* x) const {
    int best = 0;
    fix best_score = 0;
    bool first = true;
    for (int c = 0; c < classes; c++) {
        fix score = logprior[c];
        for (int j = 0; j < d; j++) {
            fix mu = mean[(size_t)c*d + j];
            fix v = var[(size_t)c*d + j];
            fix diff = x[j] - mu;
            // -0.5*ln(var) - diff^2/(2*var)
            score -= fx::fx_div(fx::fx_ln(v), fx::itofix(2));
            fix d2 = fx::fx_mul(diff, diff);
            score -= fx::fx_div(d2, fx::fx_mul(fx::itofix(2), v));
        }
        if (first || score > best_score) { best_score = score; best = c; first = false; }
    }
    return best;
}

fix GaussianNB::accuracy(const fix* Xflat, const int* yc, int n) const {
    int correct = 0;
    for (int i = 0; i < n; i++) if (predict(Xflat + (size_t)i*d) == yc[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(n));
}

// ==================== 多项式 NB ====================
MultinomialNB::MultinomialNB() : classes(0), d(0), logprob(0), logprior(0), alpha(fx::FX_ONE) {}
MultinomialNB::~MultinomialNB() { release(); }
void MultinomialNB::release() {
    if (logprob) delete[] logprob;
    if (logprior) delete[] logprior;
    logprob = 0; logprior = 0; classes = 0; d = 0;
}

void MultinomialNB::fit(const fix* Xcount, const int* yc, int n) {
    release();
    int mx = 0;
    for (int i = 0; i < n; i++) if (yc[i] > mx) mx = yc[i];
    classes = mx + 1;
    if (d <= 0) d = 2;
    logprob = new fix[(size_t)classes * d];
    logprior = new fix[classes];
    int* cnt = new int[classes];
    fix* class_total = new fix[classes];
    for (int c = 0; c < classes; c++) { cnt[c] = 0; class_total[c] = 0; }
    for (int i = 0; i < n; i++) cnt[yc[i]]++;
    for (int c = 0; c < classes; c++) logprior[c] = fx::fx_ln(fx::fx_div(fx::itofix(cnt[c]), fx::itofix(n)));
    for (int c = 0; c < classes; c++) {
        for (int j = 0; j < d; j++) {
            fix s = 0;
            for (int i = 0; i < n; i++) if (yc[i] == c) s += Xcount[(size_t)i*d + j];
            logprob[(size_t)c*d + j] = s + alpha;   // 平滑后的计数（未归一化）
            class_total[c] += s;
        }
        class_total[c] += fx::fx_mul(alpha, fx::itofix(d));
        for (int j = 0; j < d; j++)
            logprob[(size_t)c*d + j] = fx::fx_ln(fx::fx_div(logprob[(size_t)c*d + j], class_total[c]));
    }
    delete[] cnt; delete[] class_total;
}

int MultinomialNB::predict(const fix* xcount) const {
    int best = 0; fix bs = 0; bool first = true;
    for (int c = 0; c < classes; c++) {
        fix score = logprior[c];
        for (int j = 0; j < d; j++)
            score += fx::fx_mul(xcount[j], logprob[(size_t)c*d + j]);
        if (first || score > bs) { bs = score; best = c; first = false; }
    }
    return best;
}

// ==================== 伯努利 NB ====================
BernoulliNB::BernoulliNB() : classes(0), d(0), logprob(0), logprob_neg(0), logprior(0) {}
BernoulliNB::~BernoulliNB() { release(); }
void BernoulliNB::release() {
    if (logprob) delete[] logprob;
    if (logprob_neg) delete[] logprob_neg;
    if (logprior) delete[] logprior;
    logprob = logprob_neg = 0; logprior = 0; classes = 0; d = 0;
}

void BernoulliNB::fit(const fix* Xbin, const int* yc, int n) {
    release();
    int mx = 0;
    for (int i = 0; i < n; i++) if (yc[i] > mx) mx = yc[i];
    classes = mx + 1;
    if (d <= 0) d = 2;
    logprob = new fix[(size_t)classes * d];
    logprob_neg = new fix[(size_t)classes * d];
    logprior = new fix[classes];
    int* cnt = new int[classes];
    for (int c = 0; c < classes; c++) cnt[c] = 0;
    for (int i = 0; i < n; i++) cnt[yc[i]]++;
    for (int c = 0; c < classes; c++) logprior[c] = fx::fx_ln(fx::fx_div(fx::itofix(cnt[c]), fx::itofix(n)));
    for (int c = 0; c < classes; c++) {
        for (int j = 0; j < d; j++) {
            int ones = 0;
            for (int i = 0; i < n; i++) if (yc[i] == c && Xbin[(size_t)i*d + j] != 0) ones++;
            fix p = fx::fx_div(fx::itofix(ones + 1), fx::itofix(cnt[c] + 2));   // 平滑
            logprob[(size_t)c*d + j] = fx::fx_ln(p);
            logprob_neg[(size_t)c*d + j] = fx::fx_ln(fx::FX_ONE - p);
        }
    }
    delete[] cnt;
}

int BernoulliNB::predict(const fix* xbin) const {
    int best = 0; fix bs = 0; bool first = true;
    for (int c = 0; c < classes; c++) {
        fix score = logprior[c];
        for (int j = 0; j < d; j++) {
            if (xbin[j] != 0) score += logprob[(size_t)c*d + j];
            else score += logprob_neg[(size_t)c*d + j];
        }
        if (first || score > bs) { bs = score; best = c; first = false; }
    }
    return best;
}

// ---------------- 自检 ----------------
int bayes_self_test() {
    int fails = 0;

    // 两个高斯簇：类0 中心 (0,0)，类1 中心 (5,5)
    fix X[8][2] = {
        {fx::itofix(0), fx::itofix(0)},
        {fx::itofix(1), fx::itofix(0)},
        {fx::itofix(0), fx::itofix(1)},
        {fx::itofix(-1), fx::itofix(0)},
        {fx::itofix(5), fx::itofix(5)},
        {fx::itofix(6), fx::itofix(5)},
        {fx::itofix(5), fx::itofix(6)},
        {fx::itofix(4), fx::itofix(5)},
    };
    int y[8] = {0,0,0,0,1,1,1,1};
    fix flat[16];
    for (int i = 0; i < 8; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }

    GaussianNB gnb;
    gnb.d = 2;
    gnb.fit(flat, y, 8);
    fix acc = gnb.accuracy(flat, y, 8);
    if (acc < fx::fxf(90, 100)) fails++;
    // 新点 (-1,-1) 应类0，(6,6) 应类1
    fix q0[2] = {-fx::itofix(1), -fx::itofix(1)};
    fix q1[2] = { fx::itofix(6), fx::itofix(6)};
    if (gnb.predict(q0) != 0) fails++;
    if (gnb.predict(q1) != 1) fails++;

    // 伯努利 NB：二值特征。类0 特征[1,0]，类1 特征[0,1]
    fix bx[6][2] = {
        {fx::FX_ONE, 0}, {fx::FX_ONE, 0}, {fx::FX_ONE, 0},
        {0, fx::FX_ONE}, {0, fx::FX_ONE}, {0, fx::FX_ONE},
    };
    int by[6] = {0,0,0,1,1,1};
    fix bflat[12];
    for (int i = 0; i < 6; i++) { bflat[i*2] = bx[i][0]; bflat[i*2+1] = bx[i][1]; }
    BernoulliNB bnb;
    bnb.d = 2;
    bnb.fit(bflat, by, 6);
    fix t1[2] = {fx::FX_ONE, 0};
    fix t2[2] = {0, fx::FX_ONE};
    if (bnb.predict(t1) != 0) fails++;
    if (bnb.predict(t2) != 1) fails++;

    return fails;
}

} // namespace ml
} // namespace nefu
