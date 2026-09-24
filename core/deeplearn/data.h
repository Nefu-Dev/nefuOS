// nefuOS 深度学习库 —— 微型数据集工具
// 典型用法：
//   DataSet ds = make_regression(N, D);
//   ds.shuffle();
//   Tensor xb = ds.batch_x(bi, bs);
//   Tensor yb = ds.batch_y(bi, bs);
//
// 内存：DataSet 持有拷贝后的张量，不共享调用方缓冲。
// shuffle 用 LCG（确定性），便于复现。
// normalize_columns 做 z-score：(x-mean)/sqrt(var+eps)。
// 不依赖 STL，用 new[] 持有样本张量；提供按批迭代、归一化、打乱（LCG）。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 一个 (x, y) 样本对
struct Sample {
    Tensor x;     // 单样本输入
    Tensor y;     // 单样本标签（回归为标量/向量，分类为类别下标存于 y.data[0]）
};

// 微型数据集：持有 N 个样本，提供 batch 切片。
struct DataSet {
    int N;
    Tensor X;     // [N, ...] 全体输入
    Tensor Y;     // [N, ...] 全体标签
    DataSet() : N(0) {}
    // 取第 i 个样本的输入行（返回 [1, feat] 视图拷贝）
    Tensor sample_x(int i) const;
    fix    sample_y(int i) const { return Y.data[i]; }
};

// 用扁平数组构造回归数据集：x:[N,F]，y:[N,1]
DataSet make_regression(const fix* xflat, const fix* yflat, int N, int F);

// 按 batch 取一批输入：返回 [B, F] 张量（拷贝）
Tensor batch_x(const DataSet& ds, int start, int batch);
// 按 batch 取一批标签：返回 [B,1]
Tensor batch_y(const DataSet& ds, int start, int batch);

// 简单 LCG 打乱顺序（原地重排 idx 数组，长度 N）
void shuffle_idx(int* idx, int N, uint32_t seed);

// 对 [N,F] 张量按列做 z-score 归一化（返回新张量，均值/方差写入 out_mean/out_var）
Tensor normalize_columns(const Tensor& x, fix* out_mean, fix* out_var, int F);

int data_self_test();

} // namespace deeplearn
} // namespace nefu
