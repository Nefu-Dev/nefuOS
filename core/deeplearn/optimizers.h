// nefuOS 深度学习库 —— 优化器
// SGD+momentum / Adam / AdamW / RMSprop / AdaGrad / 学习率调度。
// 优化器持有参数张量指针列表，step() 用 grad 更新 data 并清 grad。
//
// 基类约定：
//   * params 持有 Tensor* 裸指针（生命期由调用方保证）；
//   * step() 用 grad 更新 data，不分配新张量；
//   * zero_grad() 把所有参数 grad 清零（朴素循环防 memset 优化）；
//   * 析构时释放每个参数的动量/二阶矩缓冲（fix** 数组）。
//
// 学习率：fix 定点，base_lr 常用 fxf(1,100)=0.01。
// SGD+momentum: v = mom*v + g; w -= lr*v
// Adam:         m=b1*m+(1-b1)*g; v=b2*v+(1-b2)*g^2; w-=lr*m_hat/(sqrt(v_hat)+eps)
// AdamW:        同 Adam，但权重衰减解耦：w -= lr*wd*w
// RMSprop:      v=a*v+(1-a)*g^2; w-=lr*g/(sqrt(v)+eps)
// AdaGrad:      v+=g^2; w-=lr*g/(sqrt(v)+eps)
// 典型用法：Adam opt(lr); opt.add(&w); opt.step(); opt.zero_grad();
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
