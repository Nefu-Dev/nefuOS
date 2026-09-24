// nefuOS 模拟库 simlib —— STL 风格聚合头
// 一次 include 全部模拟模块。自测：sim_all_self_test() 汇总各模块失败数。
// 教学版：全部用 class / do-while / switch / std 模板 / cmath 实现，中文注释。
#pragma once

#include "simlib/life.h"
#include "simlib/queue.h"
#include "simlib/world.h"
#include "simlib/boids.h"
#include "simlib/epidemic.h"
#include "simlib/traffic.h"
#include "simlib/perlin.h"
#include "simlib/langton.h"

namespace nefu {
namespace simx {

// 汇总自测：返回失败总数，0 表示全部通过
inline int sim_all_self_test() {
    return Life::self_test() + EventSim::self_test() +
           World2D::self_test() + Boids::self_test() +
           Epidemic::self_test() + TrafficFlow::self_test() +
           PerlinNoise::self_test() + LangtonAnt::self_test();
}

} // namespace simx
} // namespace nefu
