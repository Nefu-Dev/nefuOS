// nefuOS 机器学习库 —— 数据集管理
// 负责：特征矩阵 / 标签的持有、训练-测试集划分、归一化（min-max / z-score）、
//       one-hot 编码、类别计数与分层抽样辅助。
// 存储：行主序 X[n*d]，分类标签 yc[n]（int），回归目标 yr[n]（fx）。
// 归一化参数（每维 min/max 或 mean/std）单独保存，便于用同一参数变换测试集。
#pragma once
#include <stdint.h>
#include "../klib/klib.h"
#include "ml_util.h"

namespace nefu {
namespace ml {

// 数据集：分类与回归标签共用同一特征矩阵，按需取 yc 或 yr。
struct DataSet {
    int   n;          // 样本数
    int   d;          // 特征维数
    int   classes;    // 类别数（回归任务填 0）
    fix*  X;          // n*d，行主序
    int*  yc;         // n，分类标签
    fix*  yr;         // n，回归目标

    DataSet();
    ~DataSet();
    void init(int n_, int d_, int classes_);     // 分配内存（未填值）
    void release();
    bool valid() const { return n > 0 && d > 0 && X != 0; }
    fix* row(int i) { return X + (size_t)i * d; }
    const fix* row(int i) const { return X + (size_t)i * d; }
};

// ---------------- 训练/测试划分 ----------------
// 按比例 test_frac（0..1，Q16.16）随机打乱后划分。
// train_idx / test_idx 为出参数组（长度 n，调用方分配）；返回 (训练数, 测试数)。
void train_test_split(const DataSet& ds, fix test_frac, uint32_t seed,
                      int* train_idx, int* test_idx, int* n_train, int* n_test);

// K 折交叉验证：把 n 个样本打乱后分成 k 份。
// folds 出参：长度 k*n 的折叠归属，folds[i] 表示第 i 个样本落在第几折。
void kfold_indices(int n, int k, uint32_t seed, int* folds);

// ---------------- 归一化参数 ----------------
struct NormParams {
    int   d;
    fix*  mn;     // 每维最小值
    fix*  mx;     // 每维最大值
    fix*  mean;   // 每维均值（z-score 用）
    fix*  std;    // 每维标准差（z-score 用）
    NormParams();
    ~NormParams();
    void alloc(int d);
    void release();
};

// min-max：把每维线性映射到 [0,1]。fit 在训练集上估计，apply 变换任意行。
void norm_minmax_fit(const DataSet& ds, const int* idx, int n_idx, NormParams* p);
void norm_minmax_apply(const NormParams& p, fix* row_d);   // 原地变换一行（长度 d）

// z-score：(x-mean)/std。std 过小则钳为 1，避免除零。
void norm_zscore_fit(const DataSet& ds, const int* idx, int n_idx, NormParams* p);
void norm_zscore_apply(const NormParams& p, fix* row_d);

// ---------------- one-hot 编码 ----------------
// 把类别 c（0..classes-1）写到 out（长度 classes），目标位置为 1，其余 0。
void one_hot(int c, int classes, fix* out);
// 反向：取最大概率下标作为类别。
int  one_hot_argmax(const fix* v, int classes);

// ---------------- 类别统计 ----------------
// 统计每类样本数，写入 count（长度 classes，调用方分配）。
void class_counts(const DataSet& ds, int* count, int classes);
// 数据集里类别总数（去重后）。
int  num_classes_present(const DataSet& ds);

// ---------------- 自检 ----------------
// 已知值：对 y=2x+1 的小样本做归一化后范围应落在 [0,1]；
// one_hot(2,3) = [0,0,1]；划分后训练+测试数 == n。
int dataset_self_test();

} // namespace ml
} // namespace nefu
