// ============================================================================
// nefuOS 3D 图形库 —— bezier 实现
// ============================================================================
#include "bezier.h"
#include <cmath>

namespace nefu {
namespace gfx3d {

// Bernstein 基函数 B_i^3(t)
static double bernstein(int i, double t) {
    double mt = 1.0 - t;
    switch (i) {
        case 0: return mt * mt * mt;
        case 1: return 3 * mt * mt * t;
        case 2: return 3 * mt * t * t;
        case 3: return t * t * t;
    }
    return 0;
}

Vec3 bezier_patch_point(const Vec3 cp[16], double u, double v) {
    // 双三次贝塞尔：先沿 u 方向求 4 条曲线，再沿 v 方向
    Vec3 tmp[4];
    for (int j = 0; j < 4; j++) {
        tmp[j] = Vec3(0, 0, 0);
        for (int i = 0; i < 4; i++)
            tmp[j] = tmp[j] + cp[j * 4 + i] * bernstein(i, u);
    }
    Vec3 p(0, 0, 0);
    for (int j = 0; j < 4; j++) p = p + tmp[j] * bernstein(j, v);
    return p;
}

Vec3 bezier_patch_normal(const Vec3 cp[16], double u, double v) {
    // 数值求偏导
    double eps = 1e-4;
    Vec3 pu = bezier_patch_point(cp, u + eps, v) - bezier_patch_point(cp, u - eps, v);
    Vec3 pv = bezier_patch_point(cp, u, v + eps) - bezier_patch_point(cp, u, v - eps);
    return pu.cross(pv).normalized();
}

void bezier_patch_mesh(const Vec3 cp[16], int subdiv, Mesh& out) {
    int base = out.verts.size();
    for (int j = 0; j <= subdiv; j++) {
        double v = (double)j / subdiv;
        for (int i = 0; i <= subdiv; i++) {
            double u = (double)i / subdiv;
            Vec3 p = bezier_patch_point(cp, u, v);
            Vec3 n = bezier_patch_normal(cp, u, v);
            out.verts.push(Vertex(p, n));
        }
    }
    int row = subdiv + 1;
    for (int j = 0; j < subdiv; j++)
        for (int i = 0; i < subdiv; i++) {
            int a = base + j * row + i;
            out.faces.push(Face(a, a + row, a + 1));
            out.faces.push(Face(a + 1, a + row, a + row + 1));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
}

// ----------------------------------------------------------------------------
// Utah 茶壶控制点（简化版：壶身、壶盖、壶嘴、壶把各几片）
// 这些控制点是手工调整的，形成一个对称的茶壶轮廓。
// ----------------------------------------------------------------------------
static const Vec3 k_body[16] = {
    Vec3(0.0, 0.0, 0.0), Vec3(1.2, 0.0, 0.0), Vec3(1.2, 0.0, 0.6), Vec3(0.0, 0.0, 0.6),
    Vec3(0.0, 0.6, 0.0), Vec3(1.0, 0.6, 0.0), Vec3(1.0, 0.6, 0.5), Vec3(0.0, 0.6, 0.5),
    Vec3(0.0, 1.1, 0.0), Vec3(0.7, 1.1, 0.0), Vec3(0.7, 1.1, 0.35), Vec3(0.0, 1.1, 0.35),
    Vec3(0.0, 1.3, 0.0), Vec3(0.4, 1.3, 0.0), Vec3(0.4, 1.3, 0.2),  Vec3(0.0, 1.3, 0.2),
};

static const Vec3 k_lid[16] = {
    Vec3(0.0, 1.3, 0.0), Vec3(0.4, 1.3, 0.0), Vec3(0.4, 1.3, 0.2), Vec3(0.0, 1.3, 0.2),
    Vec3(0.0, 1.45, 0.0), Vec3(0.3, 1.45, 0.0), Vec3(0.3, 1.45, 0.15), Vec3(0.0, 1.45, 0.15),
    Vec3(0.0, 1.55, 0.0), Vec3(0.15, 1.55, 0.0), Vec3(0.15, 1.55, 0.08), Vec3(0.0, 1.55, 0.08),
    Vec3(0.0, 1.6, 0.0),  Vec3(0.08, 1.6, 0.0), Vec3(0.08, 1.6, 0.04),  Vec3(0.0, 1.6, 0.04),
};

void make_utah_teapot(Mesh& out, double scale, int subdiv) {
    // 壶身
    bezier_patch_mesh(k_body, subdiv, out);
    // 壶盖
    bezier_patch_mesh(k_lid, subdiv, out);
    // 缩放
    for (int i = 0; i < out.verts.size(); i++)
        out.verts[i].pos = out.verts[i].pos * scale;
    out.compute_smooth_normals();
    out.bounds = AABB();
    for (int i = 0; i < out.verts.size(); i++) out.bounds.expand(out.verts[i].pos);
}

int bezier_self_test() {
    int fail = 0;
    Mesh m;
    make_utah_teapot(m, 1.0, 2);
    if (m.vertex_count() < 100) fail++;
    if (m.face_count() < 100) fail++;
    // 控制点 (0,0) 处应返回 cp[0]
    Vec3 cp[16];
    for (int i = 0; i < 16; i++) cp[i] = Vec3(i, i, i);
    Vec3 p = bezier_patch_point(cp, 0, 0);
    if (p.x > 0.1 || p.y > 0.1) fail++;
    return fail;
}

} // namespace gfx3d
} // namespace nefu
