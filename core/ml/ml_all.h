// nefuOS 机器学习库 —— 聚合头
// 一次性引入全部 ML 模块，并提供统一的汇总自检入口 ml_self_test()。
#pragma once

#include "matrix.h"
#include "dataset.h"
#include "linear.h"
#include "knn.h"
#include "decisiontree.h"
#include "randomforest.h"
#include "kmeans.h"
#include "pca.h"
#include "neuralnet.h"
#include "bayes.h"

namespace nefu {
namespace ml {

// 运行所有子模块自检，返回总失败条数（0 = 全部通过）。
inline int ml_self_test() {
    int f = 0;
    f += matrix_self_test();
    f += dataset_self_test();
    f += linear_self_test();
    f += knn_self_test();
    f += decisiontree_self_test();
    f += randomforest_self_test();
    f += kmeans_self_test();
    f += kmedoids_self_test();
    f += pca_self_test();
    f += neuralnet_self_test();
    f += bayes_self_test();
    return f;
}

} // namespace ml
} // namespace nefu
