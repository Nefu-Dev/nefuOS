// nefuOS 深度学习库 —— 优化器
// SGD+momentum / Adam / AdamW / RMSprop / AdaGrad / 学习率调度。
// 优化器持有参数张量指针列表，step() 用 grad 更新 data 并清 grad。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 优化器基类
struct Optimizer {
    List<Tensor*> params;
    fix lr;
    Optimizer(fix learning_rate) : lr(learning_rate) {}
    virtual ~Optimizer() {}
    void add(Tensor* p) { params.push(p); }
    // 收集一组参数
    void add_all(List<Tensor*>& ps) {
        for (int i = 0; i < ps.size(); i++) params.push(ps[i]);
    }
    virtual void step() = 0;
    void zero_grad() {
        for (int i = 0; i < params.size(); i++) params[i]->zero_grad();
    }
};

// SGD + 动量
struct SGD : Optimizer {
    fix momentum;
    fix dampening;
    fix** vel;       // 每个参数的速度缓冲
    SGD(fix lr, fix mom = fx::fxf(9,10)) : Optimizer(lr), momentum(mom), dampening(mom), vel(0) {}
    void step() override;
    ~SGD();
};

// Adam
struct Adam : Optimizer {
    fix beta1, beta2, eps;
    int t;
    fix** m; fix** v;   // 每个参数的一阶/二阶矩
    Adam(fix lr = fx::fxf(1,1000), fix b1 = fx::fxf(9,10), fix b2 = fx::fxf(999,1000));
    void step() override;
    ~Adam();
};

// AdamW（带权重衰减解耦）
struct AdamW : Optimizer {
    fix beta1, beta2, eps, weight_decay;
    int t;
    fix** m; fix** v;
    AdamW(fix lr = fx::fxf(1,1000), fix wd = fx::fxf(1,1000));
    void step() override;
    ~AdamW();
};

// RMSprop
struct RMSprop : Optimizer {
    fix alpha, eps;
    fix** v;
    RMSprop(fix lr = fx::fxf(1,100), fix a = fx::fxf(9,10));
    void step() override;
    ~RMSprop();
};

// AdaGrad
struct AdaGrad : Optimizer {
    fix eps;
    fix** v;
    AdaGrad(fix lr = fx::fxf(1,100));
    void step() override;
    ~AdaGrad();
};

// 学习率调度：按步数线性衰减
fix lr_step(fix base_lr, int step, int total_steps, fix final_ratio = fx::fxf(1,100));

int optimizers_self_test();

} // namespace deeplearn
} // namespace nefu
