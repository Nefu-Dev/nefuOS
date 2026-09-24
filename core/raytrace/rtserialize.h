// ============================================================================
// nefuOS 光线追踪引擎 —— rtserialize: 场景文本序列化
// ----------------------------------------------------------------------------
// 把场景导出为可读文本（host 调试/存档）：
//   - 逐图元输出类型与参数
//   - 逐材质输出类型与颜色
//   - 逐光源输出类型与位置
// 用 nefu::String 拼接，无 STL。ksprintf 不支持 %f，全部 %d（Q16.16 原值）。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "scene.h"
#include "../klib/klib.h"

namespace nefu {
namespace raytrace {

// 把场景序列化为文本，写入 out（自动扩容）
void serialize_scene(const Scene& sc, nefu::String& out);
// 统计场景统计信息：图元数/材质数/光源数/包围盒体积
void scene_stats(const Scene& sc, int& prims, int& mats, int& lights, RTAABB& world);
// 把颜色 Q16.16 转成 "r,g,b" 文本
void serialize_color(const RTVec3& c, nefu::String& out);

int rtserialize_self_test();

} // namespace raytrace
} // namespace nefu
