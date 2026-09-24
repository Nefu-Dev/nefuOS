// nefuOS 深度学习库 —— 聚合头
// 包含全部子模块，并提供 deeplearn_self_test() 汇总。
#pragma once

#include "tensor.h"
#include "autograd.h"
#include "activations.h"
#include "losses.h"
#include "layers.h"
#include "optimizers.h"
#include "rnn.h"
#include "attention.h"
#include "models.h"
#include "data.h"
#include "trainer.h"
#include "weights.h"
#include "metrics.h"

namespace nefu {
namespace deeplearn {

// 运行全部子模块自检，返回失败总数（应为 0）。
inline int deeplearn_self_test() {
    int f = 0;
    f += tensor_self_test();
    f += autograd_self_test();
    f += activations_self_test();
    f += losses_self_test();
    f += layers_self_test();
    f += optimizers_self_test();
    f += rnn_self_test();
    f += attention_self_test();
    f += models_self_test();
    f += data_self_test();
    f += trainer_self_test();
    f += weights_self_test();
    f += metrics_self_test();
    return f;
}

} // namespace deeplearn
} // namespace nefu
