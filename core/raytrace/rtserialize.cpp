// ============================================================================
// nefuOS 光线追踪引擎 —— rtserialize 实现（Q16.16 定点）
// ============================================================================
#include "rtserialize.h"

namespace nefu {
namespace raytrace {

void serialize_color(const RTVec3& c, nefu::String& out) {
    char buf[32];
    ksprintf(buf, sizeof(buf), "%d,%d,%d", (int)c.x, (int)c.y, (int)c.z);
    out += buf;
}

static const char* prim_type_name(int t) {
    switch (t) {
    case 0: return "sphere";
    case 1: return "plane";
    case 2: return "triangle";
    case 3: return "box";
    case 4: return "torus";
    case 5: return "cylinder";
    case 6: return "cone";
    case 7: return "disc";
    default: return "unknown";
    }
}

static const char* mat_type_name(int t) {
    switch (t) {
    case 0: return "lambert";
    case 1: return "metal";
    case 2: return "dielectric";
    case 3: return "emissive";
    case 4: return "mirror";
    case 5: return "glossy";
    case 6: return "aniso";
    default: return "unknown";
    }
}

static const char* light_type_name(int t) {
    switch (t) {
    case 0: return "point";
    case 1: return "directional";
    case 2: return "spot";
    case 3: return "area";
    case 4: return "ambient";
    default: return "unknown";
    }
}

void serialize_scene(const Scene& sc, nefu::String& out) {
    char buf[128];
    out += "# nefuOS raytrace scene dump\n";
    ksprintf(buf, sizeof(buf), "primitives=%d materials=%d lights=%d\n",
             sc.prims.size(), sc.mats.size(), sc.lights.size());
    out += buf;
    // 材质
    out += "--- materials ---\n";
    for (int i = 0; i < sc.mats.size(); i++) {
        ksprintf(buf, sizeof(buf), "mat[%d] type=%s color=", i, mat_type_name(sc.mats[i].type));
        out += buf;
        serialize_color(sc.mats[i].albedo, out);
        out += "\n";
    }
    // 光源
    out += "--- lights ---\n";
    for (int i = 0; i < sc.lights.size(); i++) {
        ksprintf(buf, sizeof(buf), "light[%d] type=%s pos=%d,%d,%d color=",
                 i, light_type_name(sc.lights[i].type),
                 (int)sc.lights[i].pos.x, (int)sc.lights[i].pos.y, (int)sc.lights[i].pos.z);
        out += buf;
        serialize_color(sc.lights[i].color, out);
        out += "\n";
    }
}

void scene_stats(const Scene& sc, int& prims, int& mats, int& lights, RTAABB& world) {
    prims = sc.prims.size();
    mats = sc.mats.size();
    lights = sc.lights.size();
    world = RTAABB();
    for (int i = 0; i < sc.prims.size(); i++) world.expand(sc.prims[i].bounds());
}

int rtserialize_self_test() {
    int fail = 0;
    Scene sc;
    sc.load_preset(SCENE_SPHERES);
    nefu::String s;
    serialize_scene(sc, s);
    // 序列化应非空
    if (s.len() <= 0) fail++;
    // 统计
    int p, m, l; RTAABB w;
    scene_stats(sc, p, m, l, w);
    if (p <= 0 || m <= 0) fail++;
    // 颜色序列化
    nefu::String c;
    serialize_color(RTVec3(RT_ONE,0,0), c);
    if (c.len() <= 0) fail++;
    return fail;
}

} // namespace raytrace
} // namespace nefu
