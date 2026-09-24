// gameengine_all.h —— 2D 游戏引擎聚合头 + 汇总自测
//
// 命名空间 nefu::gameengine。bare 模式无 FPU，全部 Q16.16 定点。
// 禁止 STL 容器/异常/RTTI/malloc。
#pragma once

#include "ge_math.h"
#include "sprite.h"
#include "tilemap.h"
#include "physics2d.h"
#include "scene.h"
#include "input.h"
#include "audio_engine.h"
#include "particle_engine.h"
#include "ui.h"

namespace nefu {
namespace gameengine {

// 汇总所有模块自测；返回总失败数
inline int gameengine_self_test() {
    int f = 0;
    f += ge_math_self_test();
    f += sprite_self_test();
    f += tilemap_self_test();
    f += physics2d_self_test();
    f += scene_self_test();
    f += input_self_test();
    f += audio_engine_self_test();
    f += particle_engine_self_test();
    f += ui_self_test();
    return f;
}

} // namespace gameengine
} // namespace nefu
