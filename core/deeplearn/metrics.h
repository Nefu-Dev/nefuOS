// nefuOS 深度学习库 —— 评估指标
// 分类准确率、二分类精确率/召回率/F1、回归 MAE/RMSE。全定点。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 二分类准确率：pred 经 sigmoid 后 >0.5 判正，与 labels(0/1) 比较。
// pred[N] 原始 logits，labels[N] 0/1。返回准确率 0..1（Q16.16）。
fix metric_accuracy_binary(const Tensor& pred, const uint8_t* labels, int N);

// 多分类准确率：取每行 argmax，与真实类别比较。
// logits[N,C]，labels[N] 类别下标。
fix metric_accuracy(const Tensor& logits, const int* labels, int N);

// 二分类 MAE：mean(|pred - labels|)
fix metric_mae(const Tensor& pred, const fix* labels, int N);

// 二分类混淆矩阵：写入 tp/fp/tn/fn（计数）。
void metric_confusion(const Tensor& pred, const uint8_t* labels, int N,
                      int* tp, int* fp, int* tn, int* fn);

// F1：2*p*r/(p+r)，输入为 tp/fp/fn 计数。
fix metric_f1(int tp, int fp, int fn);

int metrics_self_test();

} // namespace deeplearn
} // namespace nefu
