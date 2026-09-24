// nefuOS 深度学习库 —— 优化器实现
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "optimizers.h"
#include "autograd.h"

namespace nefu {
namespace deeplearn {

namespace {
void clear_buf(fix* p, int n){if(!p)return;for(int i=0;i<n;i++)p[i]=0;}
// 惰性分配第 i 个参数的状态缓冲
fix** ensure_state(fix**& table, Optimizer& opt) {
    if (!table) {
        table = new fix*[opt.params.size()];
        for (int i = 0; i < opt.params.size(); i++) table[i] = nullptr;
    }
    return table;
}
} // namespace

// ==================== SGD ====================
SGD::~SGD() {
    if (vel) {
        for (int i = 0; i < params.size(); i++) if (vel[i]) delete[] vel[i];
        delete[] vel; vel = nullptr;
    }
}
void SGD::step() {
    ensure_state(vel, *this);
    for (int i = 0; i < params.size(); i++) {
        Tensor* p = params[i];
        if (!p->grad) continue;
        if (!vel[i]) { vel[i] = new fix[p->size]; clear_buf(vel[i], p->size); }
        for (int j = 0; j < p->size; j++) {
            // v = mom*v + (1-damp)*g
            fix v = fx::fx_mul(momentum, vel[i][j]) + fx::fx_mul(fx::FX_ONE - dampening, p->grad[j]);
            vel[i][j] = v;
            p->data[j] -= fx::fx_mul(lr, v);
        }
    }
}

// ==================== Adam ====================
Adam::Adam(fix learning_rate, fix b1, fix b2)
    : Optimizer(learning_rate), beta1(b1), beta2(b2), eps(fx::fxf(1,100000)), t(0), m(0), v(0) {}
Adam::~Adam() {
    if (m) { for (int i=0;i<params.size();i++) if(m[i]) delete[] m[i]; delete[] m; m=nullptr; }
    if (v) { for (int i=0;i<params.size();i++) if(v[i]) delete[] v[i]; delete[] v; v=nullptr; }
}
void Adam::step() {
    ensure_state(m, *this); ensure_state(v, *this);
    t++;
    fix b1pow = fx::fx_pow(beta1, fx::itofix(t));
    fix b2pow = fx::fx_pow(beta2, fx::itofix(t));
    fix one = fx::FX_ONE;
    fix bias1 = one - b1pow;
    fix bias2 = one - b2pow;
    for (int i = 0; i < params.size(); i++) {
        Tensor* p = params[i];
        if (!p->grad) continue;
        if (!m[i]) { m[i]=new fix[p->size]; clear_buf(m[i],p->size); }
        if (!v[i]) { v[i]=new fix[p->size]; clear_buf(v[i],p->size); }
        for (int j = 0; j < p->size; j++) {
            fix g = p->grad[j];
            m[i][j] = fx::fx_mul(beta1, m[i][j]) + fx::fx_mul(one-beta1, g);
            fix g2 = fx::fx_mul(g,g);
            v[i][j] = fx::fx_mul(beta2, v[i][j]) + fx::fx_mul(one-beta2, g2);
            fix mhat = fx::fx_div(m[i][j], bias1);
            fix vhat = fx::fx_div(v[i][j], bias2);
            p->data[j] -= fx::fx_mul(lr, fx::fx_div(mhat, fx::fx_sqrt(vhat) + eps));
        }
    }
}

// ==================== AdamW ====================
AdamW::AdamW(fix learning_rate, fix wd)
    : Optimizer(learning_rate), beta1(fx::fxf(9,10)), beta2(fx::fxf(999,1000)),
      eps(fx::fxf(1,100000)), weight_decay(wd), t(0), m(0), v(0) {}
AdamW::~AdamW() {
    if (m) { for (int i=0;i<params.size();i++) if(m[i]) delete[] m[i]; delete[] m; m=nullptr; }
    if (v) { for (int i=0;i<params.size();i++) if(v[i]) delete[] v[i]; delete[] v; v=nullptr; }
}
void AdamW::step() {
    ensure_state(m, *this); ensure_state(v, *this);
    t++;
    fix b1pow = fx::fx_pow(beta1, fx::itofix(t));
    fix b2pow = fx::fx_pow(beta2, fx::itofix(t));
    fix one = fx::FX_ONE;
    fix bias1 = one - b1pow;
    fix bias2 = one - b2pow;
    for (int i = 0; i < params.size(); i++) {
        Tensor* p = params[i];
        if (!p->grad) continue;
        if (!m[i]) { m[i]=new fix[p->size]; clear_buf(m[i],p->size); }
        if (!v[i]) { v[i]=new fix[p->size]; clear_buf(v[i],p->size); }
        for (int j = 0; j < p->size; j++) {
            fix g = p->grad[j];
            m[i][j] = fx::fx_mul(beta1, m[i][j]) + fx::fx_mul(one-beta1, g);
            fix g2 = fx::fx_mul(g,g);
            v[i][j] = fx::fx_mul(beta2, v[i][j]) + fx::fx_mul(one-beta2, g2);
            fix mhat = fx::fx_div(m[i][j], bias1);
            fix vhat = fx::fx_div(v[i][j], bias2);
            // 解耦权重衰减
            p->data[j] -= fx::fx_mul(lr, fx::fx_mul(weight_decay, p->data[j]));
            p->data[j] -= fx::fx_mul(lr, fx::fx_div(mhat, fx::fx_sqrt(vhat) + eps));
        }
    }
}

// ==================== RMSprop ====================
RMSprop::RMSprop(fix learning_rate, fix a)
    : Optimizer(learning_rate), alpha(a), eps(fx::fxf(1,10000)), v(0) {}
RMSprop::~RMSprop() {
    if (v) { for (int i=0;i<params.size();i++) if(v[i]) delete[] v[i]; delete[] v; v=nullptr; }
}
void RMSprop::step() {
    ensure_state(v, *this);
    for (int i = 0; i < params.size(); i++) {
        Tensor* p = params[i];
        if (!p->grad) continue;
        if (!v[i]) { v[i]=new fix[p->size]; clear_buf(v[i],p->size); }
        for (int j = 0; j < p->size; j++) {
            fix g2 = fx::fx_mul(p->grad[j], p->grad[j]);
            v[i][j] = fx::fx_mul(alpha, v[i][j]) + fx::fx_mul(fx::FX_ONE-alpha, g2);
            p->data[j] -= fx::fx_mul(lr, fx::fx_div(p->grad[j], fx::fx_sqrt(v[i][j]) + eps));
        }
    }
}

// ==================== AdaGrad ====================
AdaGrad::AdaGrad(fix learning_rate) : Optimizer(learning_rate), eps(fx::fxf(1,100000)), v(0) {}
AdaGrad::~AdaGrad() {
    if (v) { for (int i=0;i<params.size();i++) if(v[i]) delete[] v[i]; delete[] v; v=nullptr; }
}
void AdaGrad::step() {
    ensure_state(v, *this);
    for (int i = 0; i < params.size(); i++) {
        Tensor* p = params[i];
        if (!p->grad) continue;
        if (!v[i]) { v[i]=new fix[p->size]; clear_buf(v[i],p->size); }
        for (int j = 0; j < p->size; j++) {
            fix g2 = fx::fx_mul(p->grad[j], p->grad[j]);
            v[i][j] += g2;
            p->data[j] -= fx::fx_mul(lr, fx::fx_div(p->grad[j], fx::fx_sqrt(v[i][j]) + eps));
        }
    }
}

// ==================== 调度 ====================
fix lr_step(fix base_lr, int step, int total_steps, fix final_ratio) {
    if (total_steps <= 0) return base_lr;
    fix t = fx::fx_div(fx::itofix(step), fx::itofix(total_steps));
    if (t > fx::FX_ONE) t = fx::FX_ONE;
    fix span = fx::FX_ONE - final_ratio;
    return fx::fx_mul(base_lr, fx::FX_ONE - fx::fx_mul(span, t));
}

// ---------------- 自检 ----------------
int optimizers_self_test() {
    int fails = 0;
    // 用 SGD 拟合 y = 2x+1 的单参数：w* x + b，验证几步后 loss 下降
    {
        tape_reset();
        fix xd[4] = {fx::itofix(0),fx::itofix(1),fx::itofix(2),fx::itofix(3)};
        fix yd[4] = {fx::itofix(1),fx::itofix(3),fx::itofix(5),fx::itofix(7)};
        int s1[1] = {4};
        Tensor x = t_from_flat(1,s1,xd);
        Tensor y = t_from_flat(1,s1,yd);
        fix w0[1] = {0}; fix b0[1] = {0};
        Tensor w = t_from_flat(1,(int[1]){1}, w0); requires_grad(w);
        Tensor b = t_from_flat(1,(int[1]){1}, b0); requires_grad(b);
        SGD opt(fx::fxf(1,10));
        opt.add(&w); opt.add(&b);
        fix first_loss = 0, last_loss = 0;
        for (int ep = 0; ep < 50; ep++) {
            tape_reset();
            Tensor wx = t_mul(w, x);
            Tensor pred = t_add(wx, b);
            Tensor diff = t_sub(pred, y);
            Tensor sq = t_pow2(diff);
            Tensor loss = t_mean(sq, 0);
            if (ep == 0) first_loss = loss.data[0];
            backward(loss);
            opt.step();
            opt.zero_grad();
            last_loss = loss.data[0];
        }
        if (last_loss >= first_loss) fails++;
        // w 应接近 2
        if (!fx_close(w.data[0], fx::itofix(2), fx::fxf(30,100))) fails++;
        tape_reset();
    }
    // lr 调度
    {
        fix l0 = lr_step(fx::fxf(1,10), 0, 100);
        fix l1 = lr_step(fx::fxf(1,10), 100, 100);
        if (l1 > l0) fails++;
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
