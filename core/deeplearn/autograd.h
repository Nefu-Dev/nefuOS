// nefuOS 深度学习库 —— 自动微分（动态计算图 + 磁带反向）
// 设计：
//   * 每次前向 op 创建一个 FnNode 派生对象，记录"输出 -> 输入"的反向闭包；
//   * 所有节点推入全局磁带 g_tape（List<FnNode*>）；
//   * backward(loss) 把 loss->grad 置 1，然后逆序遍历磁带调用 apply() 反向传播；
//   * 节点由磁带持有并负责释放；Tensor 自身不拥有节点（避免循环释放）。
// 约束：无异常 / 无 RTTI（但虚函数可用）/ 无 STL。
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"
#include "../klib/klib.h"

namespace nefu {
namespace deeplearn {

using fx::fix;
using fx::fix64;

struct Tensor;

// ---------------- 反向节点基类 ----------------
struct FnNode {
    virtual ~FnNode() {}
    // 计算本节点输入的梯度：从输出张量的 grad 读出上游梯度，
    // 按链式法则累加到各输入张量的 grad。
    virtual void apply() = 0;
    // 节点类型名（调试）
    virtual const char* name() const = 0;
};

// ---------------- 磁带（计算图） ----------------
// 清空磁带并释放所有节点（不清张量数据/梯度，梯度由 zero_grad 单独清）
void  tape_reset();
// 把一个节点推入磁带（获得所有权，backward 结束后随 tape_reset 释放）
void  tape_push(FnNode* n);
// 当前磁带节点数
int   tape_size();
// 从标量 loss 开始反向传播（loss->size 必须 == 1）。
// 会自动把 loss->grad 设为 1，然后逆序 apply 所有节点。
void  backward(Tensor& loss);
// 释放磁带并把所有叶子张量的 grad 清零（一步到位的训练收尾）
void  step_clear();

// ---------------- 图构建辅助 ----------------
// 标记一个叶子张量需要梯度（分配 grad 缓冲）
Tensor& requires_grad(Tensor& t);

// ---------------- 自检 ----------------
// 用一个二元表达式 y = (w*x + b)^2 验证解析梯度与数值梯度一致。
// 典型用法：tape_reset(); 前向; backward(loss); opt.step(); opt.zero_grad();
int autograd_self_test();

} // namespace deeplearn
} // namespace nefu
