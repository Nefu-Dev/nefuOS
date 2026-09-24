// nefuOS mathlib —— 快速傅里叶变换实现 + 自测
#include "mathlib/fft.h"
#include <cstdio>
#include <cmath>

namespace nefu {
namespace mathx {

int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fft(std::vector<Complex>& a, bool inverse) {
    int n = (int)a.size();
    if (n <= 1) return;
    // 位反转置换（迭代版）
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { Complex t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    // 蝶形运算：每层合并 2 个半段
    for (int len = 2; len <= n; len <<= 1) {
        double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / (double)len;
        Complex wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            Complex w(1, 0);
            for (int k = 0; k < len / 2; k++) {
                Complex u = a[i + k];
                Complex v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w = w * wlen;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; i++) a[i] = a[i].scaled(1.0 / (double)n);
    }
}

std::vector<Complex> fft_forward(const std::vector<double>& x) {
    int n = next_pow2((int)x.size());
    std::vector<Complex> a(n);
    for (int i = 0; i < (int)x.size(); i++) a[i] = Complex(x[i], 0);
    fft(a, false);
    return a;
}

std::vector<double> fft_convolve(const std::vector<double>& a, const std::vector<double>& b) {
    int n = next_pow2((int)(a.size() + b.size() - 1));
    std::vector<Complex> fa(n), fb(n);
    for (int i = 0; i < (int)a.size(); i++) fa[i] = Complex(a[i], 0);
    for (int i = 0; i < (int)b.size(); i++) fb[i] = Complex(b[i], 0);
    fft(fa, false);
    fft(fb, false);
    for (int i = 0; i < n; i++) fa[i] = fa[i] * fb[i];
    fft(fa, true);
    std::vector<double> r(a.size() + b.size() - 1);
    for (int i = 0; i < (int)r.size(); i++) r[i] = fa[i].re;
    return r;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int fft_self_test() {
    g_fails = 0;
    {
        // 往返测试：正变换 + 逆变换还原
        std::vector<Complex> a;
        for (int i = 0; i < 8; i++) a.push_back(Complex((double)(i % 3), 0));
        std::vector<Complex> orig = a;
        fft(a, false);
        fft(a, true);
        bool ok = true;
        for (int i = 0; i < 8; i++) if (!a[i].near(orig[i], 1e-9)) ok = false;
        expect("fft-roundtrip", ok);
    }
    {
        // 卷积：(1+2x) * (3+4x) = 3 + 10x + 8x^2
        std::vector<double> a, b;
        a.push_back(1); a.push_back(2);
        b.push_back(3); b.push_back(4);
        std::vector<double> r = fft_convolve(a, b);
        bool ok = r.size() == 3 && std::abs(r[0] - 3) < 1e-9 && std::abs(r[1] - 10) < 1e-9 && std::abs(r[2] - 8) < 1e-9;
        expect("fft-convolve", ok);
    }
    {
        // 单位冲激的频谱应接近常数
        std::vector<Complex> x;
        for (int i = 0; i < 4; i++) x.push_back(Complex(i == 0 ? 1.0 : 0.0, 0));
        fft(x, false);
        bool ok = true;
        for (int i = 0; i < 4; i++) if (!x[i].near(Complex(1, 0), 1e-9)) ok = false;
        expect("fft-impulse", ok);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
