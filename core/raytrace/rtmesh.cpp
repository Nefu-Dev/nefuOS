// ============================================================================
// nefuOS 光线追踪引擎 —— rtmesh 实现（Q16.16 定点）
// ============================================================================
#include "rtmesh.h"

namespace nefu {
namespace raytrace {

void RTMesh::alloc(int n, int tri_count) {
    delete[] verts; delete[] normals; delete[] indices;
    verts = new RTVec3[n];
    normals = new RTVec3[n];
    indices = new int[tri_count * 3];
    n_verts = n;
    n_tris = tri_count;
}

void RTMesh::compute_bounds() {
    box = RTAABB();
    for (int i = 0; i < n_verts; i++) box.expand(verts[i]);
}

bool RTMesh::intersect(const RTRay& ray, rtfx& t, RTVec3& normal,
                       RTVec3& point, int& out_tri) const {
    rtfx closest = ray.tmax;
    bool found = false;
    for (int i = 0; i < n_tris; i++) {
        int i0 = indices[i*3], i1 = indices[i*3+1], i2 = indices[i*3+2];
        RTVec3 a = verts[i0], b = verts[i1], c = verts[i2];
        rtfx u, v;
        rtfx tt = rt_ray_triangle(ray, a, b, c, u, v);
        if (tt > ray.tmin && tt < closest) {
            closest = tt;
            // 面法线
            RTVec3 n = (b - a).cross(c - a).normalized();
            // 若有顶点法线，按重心插值
            if (normals) {
                rtfx w = RT_ONE - u - v;
                normal = normals[i0]*w + normals[i1]*u + normals[i2]*v;
                normal = normal.normalized();
            } else {
                normal = n;
            }
            point = ray.point_at(tt);
            out_tri = i;
            found = true;
        }
    }
    t = closest;
    return found;
}

RTMesh* rt_make_cube_mesh(const RTVec3& c, rtfx e) {
    RTMesh* m = new RTMesh();
    m->alloc(8, 12);
    // 8 个顶点：±e
    for (int i = 0; i < 8; i++) {
        rtfx x = (i & 1) ? e : -e;
        rtfx y = (i & 2) ? e : -e;
        rtfx z = (i & 4) ? e : -e;
        m->verts[i] = c + RTVec3(x, y, z);
    }
    // 12 个三角形（每面 2 个）
    int idx[36] = {
        0,1,2, 1,3,2,   // -X
        4,6,5, 5,6,7,   // +X
        0,4,1, 1,4,5,   // -Y
        2,3,6, 3,7,6,   // +Y
        0,2,4, 4,2,6,   // -Z
        1,5,3, 3,5,7    // +Z
    };
    for (int i = 0; i < 36; i++) m->indices[i] = idx[i];
    // 法线：逐面近似（这里给顶点一个朝外的方向）
    for (int i = 0; i < 8; i++) {
        RTVec3 d = (m->verts[i] - c).normalized();
        m->normals[i] = d;
    }
    m->compute_bounds();
    return m;
}

RTMesh* rt_make_sphere_mesh(const RTVec3& c, rtfx r, int seg_u, int seg_v) {
    if (seg_u < 3) seg_u = 8;
    if (seg_v < 2) seg_v = 6;
    int n = (seg_u + 1) * (seg_v + 1);
    int tris = seg_u * seg_v * 2;
    RTMesh* m = new RTMesh();
    m->alloc(n, tris);
    int vi = 0;
    for (int v = 0; v <= seg_v; v++) {
        rtfx phi = rt_mul(RT_PI, rt_div(rt_itofx(v), rt_itofx(seg_v)));
        rtfx sp = fx::fx_sin(phi), cp = fx::fx_cos(phi);
        for (int u = 0; u <= seg_u; u++) {
            rtfx theta = rt_mul(RT_2PI, rt_div(rt_itofx(u), rt_itofx(seg_u)));
            rtfx st = fx::fx_sin(theta), ct = fx::fx_cos(theta);
            rtfx x = rt_mul(rt_mul(r, sp), ct);
            rtfx y = rt_mul(r, cp);
            rtfx z = rt_mul(rt_mul(r, sp), st);
            m->verts[vi] = c + RTVec3(x, y, z);
            m->normals[vi] = RTVec3(x, y, z).normalized();
            vi++;
        }
    }
    int ii = 0;
    for (int v = 0; v < seg_v; v++) {
        for (int u = 0; u < seg_u; u++) {
            int a = v * (seg_u + 1) + u;
            int b = a + seg_u + 1;
            m->indices[ii++] = a;
            m->indices[ii++] = b;
            m->indices[ii++] = a + 1;
            m->indices[ii++] = a + 1;
            m->indices[ii++] = b;
            m->indices[ii++] = b + 1;
        }
    }
    m->compute_bounds();
    return m;
}

int rtmesh_self_test() {
    int fail = 0;
    // 1. 立方体网格：射线从 +X 打中心应命中
    {
        RTMesh* m = rt_make_cube_mesh(RTVec3(0,0,0), RT_ONE);
        RTRay ray(RTVec3(rt_itofx(3),0,0), RTVec3(-RT_ONE,0,0));
        rtfx t; RTVec3 n, p; int tri;
        bool ok = m->intersect(ray, t, n, p, tri);
        if (!ok) fail++;
        if (t <= 0) fail++;
        delete m;
    }
    // 2. 球体网格：射线打中心应命中
    {
        RTMesh* m = rt_make_sphere_mesh(RTVec3(0,0,0), RT_ONE, 12, 8);
        RTRay ray(RTVec3(0,0,rt_itofx(3)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, p; int tri;
        bool ok = m->intersect(ray, t, n, p, tri);
        if (!ok) fail++;
        delete m;
    }
    // 3. 包围盒
    {
        RTMesh* m = rt_make_cube_mesh(RTVec3(0,0,0), RT_ONE);
        if (m->box.mn.x > -RT_ONE + 200) fail++;
        if (m->box.mx.x < RT_ONE - 200) fail++;
        delete m;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
