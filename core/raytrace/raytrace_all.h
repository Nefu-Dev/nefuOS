// ============================================================================
// nefuOS 光线追踪引擎 —— raytrace_all.h 聚合头
// ----------------------------------------------------------------------------
// 包含此头即可使用整个光线追踪引擎，并提供 raytrace_self_test()
// 一次性运行所有子模块自检，返回失败总数（0 = 全通过）。
//
// 命名空间：nefu::raytrace
// 依赖：core/lib/softmath.h (nefu::fx Q16.16)
//       core/klib/klib.h    (nefu::List / nefu::String)
// ============================================================================
#pragma once

#include "rtmath.h"
#include "primitives.h"
#include "materials.h"
#include "lights.h"
#include "camera.h"
#include "texture.h"
#include "bvh.h"
#include "pathtracer.h"
#include "scene.h"
#include "rtutil.h"
#include "rtmesh.h"
#include "rtsampler.h"
#include "rtaccel.h"
#include "rtonemap.h"
#include "rtbrdf.h"
#include "rtnoise.h"
#include "rtintegrator.h"
#include "rtserialize.h"
#include "rtcampath.h"
#include "rtstats.h"
#include "rtenv.h"
#include "rtbake.h"
#include "rtfilter.h"
#include "rtlightprobe.h"
#include "rtmis.h"
#include "rtinstance.h"
#include "rtlighttree.h"
#include "rtraypool.h"
#include "rtaccum.h"
#include "rtrender.h"
#include "rtshader.h"
#include "rtmesh2.h"
#include "rtworld.h"
#include "rtsky.h"
#include "rtpost.h"

namespace nefu {
namespace raytrace {

// 汇总所有子模块自检，返回失败总数
inline int raytrace_self_test() {
    int f = 0;
    f += rtmath_self_test();
    f += primitives_self_test();
    f += materials_self_test();
    f += lights_self_test();
    f += camera_self_test();
    f += texture_self_test();
    f += bvh_self_test();
    f += pathtracer_self_test();
    f += scene_self_test();
    f += rtutil_self_test();
    f += rtmesh_self_test();
    f += rtsampler_self_test();
    f += rtaccel_self_test();
    f += rtonemap_self_test();
    f += rtbrdf_self_test();
    f += rtnoise_self_test();
    f += rtintegrator_self_test();
    f += rtserialize_self_test();
    f += rtcampath_self_test();
    f += rtstats_self_test();
    f += rtenv_self_test();
    f += rtbake_self_test();
    f += rtfilter_self_test();
    f += rtlightprobe_self_test();
    f += rtmis_self_test();
    f += rtinstance_self_test();
    f += rtlighttree_self_test();
    f += rtraypool_self_test();
    f += rtaccum_self_test();
    f += rtrender_self_test();
    f += rtshader_self_test();
    f += rtmesh2_self_test();
    f += rtworld_self_test();
    f += rtsky_self_test();
    f += rtpost_self_test();
    return f;
}

} // namespace raytrace
} // namespace nefu
