// ============================================================================
// nefuOS 光线追踪引擎 —— rtmesh2 实现
// ============================================================================
#include "rtmesh2.h"

namespace nefu {
namespace raytrace {

void RTProceduralMesh::alloc(int n) {
    delete[] tris;
    cap = n; count = 0;
    tris = new Primitive[n];
}

void RTProceduralMesh::push_tri(const RTVec3& a, const RTVec3& b, const RTVec3& c, int mat) {
    if (count >= cap) return;
    tris[count++] = Primitive::make_triangle(a, b, c, mat);
}

void RTProceduralMesh::gen_plane(int n, rtfx size, int mat) {
    rtfx step = rt_div(size, rt_itofx(n));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            rtfx x0 = -rt_div(size, rt_itofx(2)) + rt_itofx(i) * step;
            rtfx x1 = x0 + step;
            rtfx z0 = -rt_div(size, rt_itofx(2)) + rt_itofx(j) * step;
            rtfx z1 = z0 + step;
            push_tri(RTVec3(x0,0,z0), RTVec3(x1,0,z0), RTVec3(x0,0,z1), mat);
            push_tri(RTVec3(x1,0,z0), RTVec3(x1,0,z1), RTVec3(x0,0,z1), mat);
        }
    }
}

void RTProceduralMesh::gen_uv_sphere(int rings, int segs, const RTVec3& c, rtfx r, int mat) {
    for (int i = 0; i < rings; i++) {
        rtfx p0 = rt_div(rt_itofx(i), rt_itofx(rings)) * RT_PI;
        rtfx p1 = rt_div(rt_itofx(i+1), rt_itofx(rings)) * RT_PI;
        for (int j = 0; j < segs; j++) {
            rtfx t0 = rt_div(rt_itofx(j), rt_itofx(segs)) * RT_PI * 2;
            rtfx t1 = rt_div(rt_itofx(j+1), rt_itofx(segs)) * RT_PI * 2;
            RTVec3 a = c + RTVec3(rt_mul(fx::fx_sin(p0),fx::fx_cos(t0)), fx::fx_cos(p0), rt_mul(fx::fx_sin(p0),fx::fx_sin(t0))) * r;
            RTVec3 b = c + RTVec3(rt_mul(fx::fx_sin(p1),fx::fx_cos(t0)), fx::fx_cos(p1), rt_mul(fx::fx_sin(p1),fx::fx_sin(t0))) * r;
            RTVec3 d = c + RTVec3(rt_mul(fx::fx_sin(p0),fx::fx_cos(t1)), fx::fx_cos(p0), rt_mul(fx::fx_sin(p0),fx::fx_sin(t1))) * r;
            push_tri(a, b, d, mat);
        }
    }
}

int rtmesh2_self_test() {
    int fail = 0;
    RTProceduralMesh m;
    m.alloc(64);
    m.gen_plane(2, rt_itofx(2), 0);
    // 1. 生成 8 个三角形
    {
        if (m.count != 8) fail++;
    }
    // 2. 球
    {
        RTProceduralMesh s;
        s.alloc(256);
        s.gen_uv_sphere(4, 6, RTVec3(0,0,0), RT_ONE, 1);
        if (s.count <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
