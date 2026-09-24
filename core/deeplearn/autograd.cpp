// nefuOS 深度学习库 —— 自动微分磁带实现
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "autograd.h"
#include "tensor.h"
#include <cstdio>

namespace nefu {
namespace deeplearn {

// 全局磁带：当前计算图的所有反向节点。
// 训练循环典型用法：
//   tape_reset();                 // 清空上一步图
//   auto loss = forward(...);
//   backward(loss);               // 反向
//   optimizer.step();            // 更新参数
//   step_clear();                 // 清梯度 + 清图
static List<FnNode*> g_tape;

void tape_reset() {
    for (int i = 0; i < g_tape.size(); i++) delete g_tape[i];
    g_tape.clear();
}

void tape_push(FnNode* n) { g_tape.push(n); }

int tape_size() { return g_tape.size(); }

void backward(Tensor& loss) {
    if (loss.size != 1) return;       // 只对标量损失反向
    loss.alloc_grad();
    loss.grad[0] = fx::FX_ONE;       // dL/dL = 1
    // 逆序传播
    for (int i = g_tape.size() - 1; i >= 0; i--)
        g_tape[i]->apply();
}

void step_clear() {
    // 节点由磁带释放；梯度由调用方对参数 zero_grad。
    tape_reset();
}

Tensor& requires_grad(Tensor& t) {
    t.alloc_grad();
    return t;
}

// ---------------- 自检 ----------------
// 验证 y = (w*x + b)^2 的解析梯度。
// 构造：w,x,b 均为标量（size=1）。y = (w*x+b)^2。
// dy/dw = 2*(w*x+b)*x
// dy/dx = 2*(w*x+b)*w
// dy/db = 2*(w*x+b)
namespace {
struct GradCtx { Tensor* w; Tensor* x; Tensor* b; };
fix loss_fn(void* ctx) {
    GradCtx* c = (GradCtx*)ctx;
    fix wx = fx::fx_mul(c->w->data[0], c->x->data[0]);
    fix s = wx + c->b->data[0];
    return fx::fx_mul(s, s);
}
} // namespace

int autograd_self_test() {
    int fails = 0;
    tape_reset();
    int s1[1] = { 1 };
    Tensor w = t_from_flat(1, s1, (fix[]){ fx::itofix(2) });
    Tensor x = t_from_flat(1, s1, (fix[]){ fx::itofix(3) });
    Tensor b = t_from_flat(1, s1, (fix[]){ fx::itofix(1) });
    requires_grad(w); requires_grad(x); requires_grad(b);

    // 手工构建：h = w*x + b ；y = h*h
    Tensor wx = t_mul(w, x);
    Tensor h  = t_add(wx, b);
    Tensor y  = t_pow2(h);
    backward(y);

    // 期望 s = w*x+b = 2*3+1 = 7；y=49
    // dy/dw = 2*7*3 = 42 ; dy/dx = 2*7*2 = 28 ; dy/db = 2*7 = 14
    fix tol = fx::fxf(5, 100);  // 0.05
    if (!fx_close(w.grad[0], fx::itofix(42), tol)) fails++;
    if (!fx_close(x.grad[0], fx::itofix(28), tol)) fails++;
    if (!fx_close(b.grad[0], fx::itofix(14), tol)) fails++;

    // 数值梯度核对
    GradCtx ctx{&w, &x, &b};
    fix eps = fx::fxf(1, 100);  // 0.01
    fix ng[3];
    // 对 w
    { fix o = w.data[0]; w.data[0]=o+eps; fix fp=loss_fn(&ctx); w.data[0]=o-eps; fix fm=loss_fn(&ctx); w.data[0]=o;
      ng[0] = fx::fx_div(fp-fm, eps+eps); }
    { fix o = x.data[0]; x.data[0]=o+eps; fix fp=loss_fn(&ctx); x.data[0]=o-eps; fix fm=loss_fn(&ctx); x.data[0]=o;
      ng[1] = fx::fx_div(fp-fm, eps+eps); }
    { fix o = b.data[0]; b.data[0]=o+eps; fix fp=loss_fn(&ctx); b.data[0]=o-eps; fix fm=loss_fn(&ctx); b.data[0]=o;
      ng[2] = fx::fx_div(fp-fm, eps+eps); }
    if (!fx_close(ng[0], w.grad[0], fx::fxf(2,10))) fails++;
    if (!fx_close(ng[1], x.grad[0], fx::fxf(2,10))) fails++;
    if (!fx_close(ng[2], b.grad[0], fx::fxf(2,10))) fails++;

    tape_reset();
    return fails;
}

} // namespace deeplearn
} // namespace nefu
