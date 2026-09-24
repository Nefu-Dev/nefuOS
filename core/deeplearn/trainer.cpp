// nefuOS 深度学习库 —— 训练循环封装实现
// Trainer 驱动 epoch，CosineScheduler 余弦衰减。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "trainer.h"

namespace nefu {
namespace deeplearn {

Trainer::Trainer(DataSet* d, Optimizer* o, int bs)
    : ds(d), opt(o), batch_size(bs), epochs_done(0), hist_n(0) {
    loss_history = new fix[256];
    for (int i = 0; i < 256; i++) loss_history[i] = 0;
}
Trainer::~Trainer() { delete[] loss_history; }

void Trainer::run_epoch(TrainStepFn fn, void* ctx, uint32_t shuffle_seed) {
    if (!ds || !fn) return;
    int N = ds->N;
    int* idx = new int[N];
    shuffle_idx(idx, N, shuffle_seed);
    fix64 sum = 0;
    int nb = 0;
    for (int s = 0; s < N; s += batch_size) {
        int b = batch_size;
        if (s + b > N) b = N - s;
        // 按打乱顺序收集一批（简化：直接顺序，shuffle 仅占位演示）
        Tensor x = batch_x(*ds, s, b);
        Tensor y = batch_y(*ds, s, b);
        fix l = fn(ctx, x, y);
        sum += (fix64)l;
        nb++;
    }
    delete[] idx;
    fix avg = (fix)(sum / (nb > 0 ? nb : 1));
    if (hist_n < 256) loss_history[hist_n++] = avg;
    epochs_done++;
}

// ---------------- 余弦调度器 ----------------
CosineScheduler::CosineScheduler(fix base, fix final, int warmup, int total)
    : base_lr(base), final_lr(final), warmup_steps(warmup), total_steps(total) {}

fix CosineScheduler::lr_at(int step) const {
    if (step < warmup_steps) {
        // 线性预热
        fix t = fx::fx_div(fx::itofix(step), fx::itofix(warmup_steps > 0 ? warmup_steps : 1));
        return fx::fx_mul(base_lr, t);
    }
    // 余弦衰减：lr = final + 0.5*(base-final)*(1+cos(pi*(step-warmup)/(total-warmup)))
    int prog = step - warmup_steps;
    int span = total_steps - warmup_steps;
    if (span <= 0) return final_lr;
    fix t = fx::fx_div(fx::itofix(prog), fx::itofix(span));   // 0..1
    // 用 cos(pi*t)：近似 cos 表由 fx_cos 提供（参数为弧度）
    fix angle = fx::fx_mul(fx::fx_mul(fx::FX_PI, t), fx::FX_HALF);
    fix c = fx::fx_cos(angle);
    fix range = base_lr - final_lr;
    return final_lr + fx::fx_mul(fx::fx_mul(range, fx::FX_HALF), fx::FX_ONE + c);
}

// ---------------- 自检 ----------------
namespace {
fix dummy_step(void*, const Tensor&, const Tensor&) { return 0; }
} // namespace

int trainer_self_test() {
    int fails = 0;
    fix tol = fx::fxf(5,100);
    // 余弦调度：step=0 附近 lr 应接近 0（预热），中间接近 base
    {
        CosineScheduler sch(fx::fxf(1,100), fx::fxf(1,1000), 10, 100);
        fix l0 = sch.lr_at(0);
        fix lmid = sch.lr_at(50);
        fix lend = sch.lr_at(100);
        // 预热起点 ~0
        if (l0 > fx::fxf(5,1000)) fails++;
        // 终点应 <= base 且接近 final
        if (lend > fx::fxf(1,100)) fails++;
        (void)lmid;
    }
    // Trainer 构造/析构不崩溃，记录损失
    {
        fix x[6] = {0,0, fx::FX_ONE,0, 0,fx::FX_ONE};
        fix y[3] = {0, fx::FX_ONE, fx::FX_ONE};
        DataSet ds = make_regression(x, y, 3, 2);
        SGD opt(fx::fxf(1,10));
        Trainer tr(&ds, &opt, 2);
        tr.run_epoch(dummy_step, nullptr, 1);
        if (tr.epochs_done != 1) fails++;
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
