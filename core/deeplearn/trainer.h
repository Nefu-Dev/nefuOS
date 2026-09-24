// nefuOS 深度学习库 —— 训练循环封装
// 典型用法：
//   Trainer tr(epochs, print_every);
//   tr.run([&](int ep){ /* 前向+反向+step */ });
//   CosineScheduler cs(base_lr, final_lr, warmup, total);
//   lr = cs.lr(step);
//
// Trainer 只驱动 epoch，不持有模型结构；fn 回调负责前向+反向+step。
// CosineScheduler：先 warmup 线性升，再余弦衰减到 final_lr。
// Trainer：把 DataSet + 参数列表 + 优化器绑在一起，按 epoch/batch 训练，
// 记录损失历史。仅负责驱动，不持有模型结构（模型 forward 由调用方提供回调）。
#pragma once
#include "tensor.h"
#include "data.h"
#include "optimizers.h"

namespace nefu {
namespace deeplearn {

// 一步前向+损失的回调：由模型提供，返回标量损失张量。
// ctx 为调用方上下文；x 为当前 batch 输入。
typedef fix (*TrainStepFn)(void* ctx, const Tensor& x, const Tensor& y);

// 简易训练器：逐 epoch 遍历数据集，每 epoch 记录平均损失。
struct Trainer {
    DataSet* ds;
    Optimizer* opt;
    int batch_size;
    int epochs_done;
    fix* loss_history;     // 每 epoch 平均损失
    int hist_n;
    Trainer(DataSet* d, Optimizer* o, int bs);
    ~Trainer();
    // 跑一个 epoch（调用方需自行在 fn 内 backward+step）
    void run_epoch(TrainStepFn fn, void* ctx, uint32_t shuffle_seed);
    // 取最近一次记录的损失
    fix last_loss() const { return hist_n>0 ? loss_history[hist_n-1] : 0; }
};

// 学习率预热 + 余弦衰减调度器：先 warmup_steps 线性升到 base，再余弦降到 final。
struct CosineScheduler {
    fix base_lr, final_lr;
    int warmup_steps, total_steps;
    CosineScheduler(fix base, fix final, int warmup, int total);
    fix lr_at(int step) const;
};

int trainer_self_test();

} // namespace deeplearn
} // namespace nefu
