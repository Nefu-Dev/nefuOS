// nefuOS 仿真引擎库 —— 聚合头
//   #include "simulate/simulate_all.h"
// 一次性引入全部仿真模块，并提供统一的 simulate_self_test()。
#pragma once

#include "ca.h"
#include "particle.h"
#include "physics.h"
#include "fluid.h"
#include "lsystem.h"
#include "flocking.h"

namespace nefu {
namespace simulate {

// 汇总所有模块自检；返回总失败数 (0 == 全部通过)
inline int simulate_self_test() {
    int f = 0;
    f += ca_self_test();
    f += particle_self_test();
    f += physics_self_test();
    f += fluid_self_test();
    f += lsystem_self_test();
    f += flocking_self_test();
    return f;
}

} // namespace simulate
} // namespace nefu
