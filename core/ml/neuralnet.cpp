#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 前馈神经网络实现（Q16.16，反向传播）
#include "neuralnet.h"
#include <cmath>

namespace nefu {
namespace ml {

NeuralNet::NeuralNet() : n_layers(0), sizes(0), W(0), B(0), a(0), z(0), trained(false) {}
NeuralNet::~NeuralNet() { release(); }

void NeuralNet::release() {
    if (W) { for (int l = 1; l < n_layers; l++) if (W[l]) delete[] W[l]; delete[] W; W = 0; }
    if (B) { for (int l = 1; l < n_layers; l++) if (B[l]) delete[] B[l]; delete[] B; B = 0; }
    if (a) { for (int l = 0; l < n_layers; l++) if (a[l]) delete[] a[l]; delete[] a; a = 0; }
    if (z) { for (int l = 0; l < n_layers; l++) if (z[l]) delete[] z[l]; delete[] z; z = 0; }
    if (sizes) delete[] sizes;
    sizes = 0; n_layers = 0; trained = false;
}

void NeuralNet::init(const int* sizes_, int L) {
    release();
    n_layers = L;
    sizes = new int[L];
    for (int i = 0; i < L; i++) sizes[i] = sizes_[i];
    W = new fix*[L];
    B = new fix*[L];
    a = new fix*[L];
    z = new fix*[L];
    for (int l = 0; l < L; l++) {
        W[l] = 0; B[l] = 0;
        a[l] = new fix[sizes[l]];
        z[l] = new fix[sizes[l]];
        for (int j = 0; j < sizes[l]; j++) { a[l][j] = 0; z[l][j] = 0; }
    }
    // 权值：He 初始化近似，用小随机数
    Rng rng(2024);
    for (int l = 1; l < L; l++) {
        int fan_in = sizes[l - 1];
        W[l] = new fix[(size_t)sizes[l] * fan_in];
        B[l] = new fix[sizes[l]];
        fix scale = fx::fxf(1, 10);     // 小随机范围 ±0.1
        for (int j = 0; j < sizes[l] * fan_in; j++)
            W[l][j] = rng.range_fx(-scale, scale);
        for (int j = 0; j < sizes[l]; j++) B[l][j] = 0;
    }
}

void NeuralNet::forward(const fix* x) {
    for (int j = 0; j < sizes[0]; j++) a[0][j] = x[j];
    int L = n_layers;
    for (int l = 1; l < L; l++) {
        int prev = sizes[l - 1], cur = sizes[l];
        for (int i = 0; i < cur; i++) {
            fix64 s = (fix64)B[l][i];
            for (int j = 0; j < prev; j++)
                s += (fix64)W[l][(size_t)i * prev + j] * (fix64)a[l - 1][j];
            z[l][i] = (fix)(s >> 16);
        }
        if (l == L - 1) {
            // softmax
            fix mx = z[l][0];
            for (int i = 1; i < cur; i++) if (z[l][i] > mx) mx = z[l][i];
            fix64 sum = 0;
            for (int i = 0; i < cur; i++) {
                fix e = fx::fx_exp(z[l][i] - mx);
                a[l][i] = e;
                sum += (fix64)e;
            }
            fix inv = fx::fx_div(fx::FX_ONE, (fix)sum);
            for (int i = 0; i < cur; i++) a[l][i] = fx::fx_mul(a[l][i], inv);
        } else {
            for (int i = 0; i < cur; i++) a[l][i] = relu(z[l][i]);
        }
    }
}

int NeuralNet::predict(const fix* x) {
    forward(x);
    int L = n_layers - 1;
    int best = 0; fix bv = a[L][0];
    for (int i = 1; i < sizes[L]; i++) if (a[L][i] > bv) { bv = a[L][i]; best = i; }
    return best;
}

void NeuralNet::fit(const fix* Xflat, const int* yc, int n, int classes,
                   fix lr, int iters) {
    int L = n_layers;
    // 梯度缓冲
    fix** gW = new fix*[L];
    fix** gB = new fix*[L];
    // delta 缓冲按最大层宽分配：反向传播时 delta 会写入隐藏层大小，
    // 不能只按输出层大小，否则隐藏层比输出层宽时越界堆损坏。
    int maxw = 1;
    for (int l = 0; l < L; l++) if (sizes[l] > maxw) maxw = sizes[l];
    fix* delta = new fix[(size_t)maxw];
    fix* prev_delta = new fix[(size_t)maxw];
    for (int l = 1; l < L; l++) {
        gW[l] = new fix[(size_t)sizes[l] * sizes[l - 1]];
        gB[l] = new fix[sizes[l]];
    }
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(n));

    for (int it = 0; it < iters; it++) {
        // 清零梯度
        for (int l = 1; l < L; l++) {
            for (int j = 0; j < sizes[l] * sizes[l - 1]; j++) gW[l][j] = 0;
            for (int j = 0; j < sizes[l]; j++) gB[l][j] = 0;
        }
        // 逐样本反向传播，累加梯度
        for (int s = 0; s < n; s++) {
            const fix* x = Xflat + (size_t)s * sizes[0];
            forward(x);
            // 输出层 delta = aL - onehot(y)
            for (int i = 0; i < sizes[L - 1]; i++)
                delta[i] = a[L - 1][i] - (yc[s] == i ? fx::FX_ONE : 0);
            // 从输出层往回
            for (int l = L - 1; l >= 1; l--) {
                int prev = sizes[l - 1], cur = sizes[l];
                for (int i = 0; i < cur; i++) gB[l][i] += delta[i];
                for (int i = 0; i < cur; i++)
                    for (int j = 0; j < prev; j++)
                        gW[l][(size_t)i * prev + j] += fx::fx_mul(delta[i], a[l - 1][j]);
                // 计算下一层 delta（若 l-1 > 0 且是隐藏层）
                if (l > 1) {
                    for (int j = 0; j < prev; j++) {
                        fix64 acc = 0;
                        for (int i = 0; i < cur; i++)
                            acc += (fix64)W[l][(size_t)i * prev + j] * (fix64)delta[i];
                        fix d = (fix)(acc >> 16);
                        prev_delta[j] = fx::fx_mul(d, relu_grad(z[l - 1][j]));
                    }
                    for (int j = 0; j < prev; j++) delta[j] = prev_delta[j];
                }
            }
        }
        // 更新：W -= lr * (grad/n)
        for (int l = 1; l < L; l++) {
            int prev = sizes[l - 1], cur = sizes[l];
            for (int i = 0; i < cur; i++) {
                B[l][i] -= fx::fx_mul(lr, fx::fx_mul(gB[l][i], invn));
                for (int j = 0; j < prev; j++)
                    W[l][(size_t)i * prev + j] -= fx::fx_mul(lr, fx::fx_mul(gW[l][(size_t)i * prev + j], invn));
            }
        }
    }
    for (int l = 1; l < L; l++) { delete[] gW[l]; delete[] gB[l]; }
    delete[] gW; delete[] gB; delete[] delta; delete[] prev_delta;
    trained = true;
}

void NeuralNet::fit_momentum(const fix* Xflat, const int* yc, int n, int classes,
                             fix lr, int iters, fix momentum) {
    int L = n_layers;
    fix** gW = new fix*[L];
    fix** gB = new fix*[L];
    fix** vW = new fix*[L];   // 速度（动量）
    fix** vB = new fix*[L];
    int maxw = 1;
    for (int l = 0; l < L; l++) if (sizes[l] > maxw) maxw = sizes[l];
    fix* delta = new fix[(size_t)maxw];
    fix* prev_delta = new fix[(size_t)maxw];
    for (int l = 1; l < L; l++) {
        gW[l] = new fix[(size_t)sizes[l] * sizes[l - 1]];
        gB[l] = new fix[sizes[l]];
        vW[l] = new fix[(size_t)sizes[l] * sizes[l - 1]];
        vB[l] = new fix[sizes[l]];
        for (int j = 0; j < sizes[l] * sizes[l - 1]; j++) { gW[l][j] = 0; vW[l][j] = 0; }
        for (int j = 0; j < sizes[l]; j++) { gB[l][j] = 0; vB[l][j] = 0; }
    }
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(n));

    for (int it = 0; it < iters; it++) {
        for (int l = 1; l < L; l++) {
            for (int j = 0; j < sizes[l] * sizes[l - 1]; j++) gW[l][j] = 0;
            for (int j = 0; j < sizes[l]; j++) gB[l][j] = 0;
        }
        for (int s = 0; s < n; s++) {
            const fix* x = Xflat + (size_t)s * sizes[0];
            forward(x);
            for (int i = 0; i < sizes[L - 1]; i++)
                delta[i] = a[L - 1][i] - (yc[s] == i ? fx::FX_ONE : 0);
            for (int l = L - 1; l >= 1; l--) {
                int prev = sizes[l - 1], cur = sizes[l];
                for (int i = 0; i < cur; i++) gB[l][i] += delta[i];
                for (int i = 0; i < cur; i++)
                    for (int j = 0; j < prev; j++)
                        gW[l][(size_t)i * prev + j] += fx::fx_mul(delta[i], a[l - 1][j]);
                if (l > 1) {
                    for (int j = 0; j < prev; j++) {
                        fix64 acc = 0;
                        for (int i = 0; i < cur; i++)
                            acc += (fix64)W[l][(size_t)i * prev + j] * (fix64)delta[i];
                        fix d = (fix)(acc >> 16);
                        prev_delta[j] = fx::fx_mul(d, relu_grad(z[l - 1][j]));
                    }
                    for (int j = 0; j < prev; j++) delta[j] = prev_delta[j];
                }
            }
        }
        // 动量更新：v = momentum*v - lr*grad/n；W += v
        for (int l = 1; l < L; l++) {
            int prev = sizes[l - 1], cur = sizes[l];
            for (int i = 0; i < cur; i++) {
                fix g = fx::fx_mul(gB[l][i], invn);
                vB[l][i] = fx::fx_mul(momentum, vB[l][i]) - fx::fx_mul(lr, g);
                B[l][i] += vB[l][i];
                for (int j = 0; j < prev; j++) {
                    fix gw = fx::fx_mul(gW[l][(size_t)i * prev + j], invn);
                    vW[l][(size_t)i * prev + j] =
                        fx::fx_mul(momentum, vW[l][(size_t)i * prev + j]) - fx::fx_mul(lr, gw);
                    W[l][(size_t)i * prev + j] += vW[l][(size_t)i * prev + j];
                }
            }
        }
    }
    for (int l = 1; l < L; l++) {
        delete[] gW[l]; delete[] gB[l]; delete[] vW[l]; delete[] vB[l];
    }
    delete[] gW; delete[] gB; delete[] vW; delete[] vB;
    delete[] delta; delete[] prev_delta;
    trained = true;
}

fix NeuralNet::accuracy(const fix* Xflat, const int* yc, int n) {
    int correct = 0;
    for (int i = 0; i < n; i++)
        if (predict(Xflat + (size_t)i * sizes[0]) == yc[i]) correct++;
    return fx::fx_div(fx::itofix(correct), fx::itofix(n));
}

// ---------------- 自检 ----------------
int neuralnet_self_test() {
    int fails = 0;

    // 两类二维点：x+y>0 类1，否则类0（线性可分）
    // 类0: (-3,-3),(-2,-1),(-1,-2)；类1: (3,3),(2,1),(1,2)
    fix X[6][2] = {
        {-fx::itofix(3), -fx::itofix(3)},
        {-fx::itofix(2), -fx::itofix(1)},
        {-fx::itofix(1), -fx::itofix(2)},
        { fx::itofix(3),  fx::itofix(3)},
        { fx::itofix(2),  fx::itofix(1)},
        { fx::itofix(1),  fx::itofix(2)},
    };
    int y[6] = {0,0,0,1,1,1};
    fix flat[12];
    for (int i = 0; i < 6; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }

    int arch[3] = {2, 4, 2};   // 输入2 -> 隐藏4(ReLU) -> 输出2(softmax)
    NeuralNet net;
    net.init(arch, 3);
    net.fit(flat, y, 6, 2, fx::fxf(5, 10), 2000);
    fix acc = net.accuracy(flat, y, 6);
    if (acc < fx::fxf(85, 100)) fails++;

    // 预测新点
    fix q0[2] = {-fx::itofix(2), -fx::itofix(2)};
    fix q1[2] = { fx::itofix(2),  fx::itofix(2)};
    if (net.predict(q0) != 0) fails++;
    if (net.predict(q1) != 1) fails++;
    // 带动量的训练也应收敛
    {
        int arch[3] = {2, 4, 2};
        NeuralNet net2;
        net2.init(arch, 3);
        net2.fit_momentum(flat, y, 6, 2, fx::fxf(5, 10), 1500, fx::fxf(9,10));
        fix acc2 = net2.accuracy(flat, y, 6);
        if (acc2 < fx::fxf(80, 100)) fails++;
    }
    return fails;
}

} // namespace ml
} // namespace nefu
