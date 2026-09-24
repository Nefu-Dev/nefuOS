// ============================================================================
// nefuOS 3D 图形库 —— shadows 实现
// ============================================================================
#include "shadows.h"
#include <cmath>
#include <cstring>

namespace nefu {
namespace gfx3d {

Mesh project_shadow_mesh(const Mesh& src, const Vec3& light_dir, double ground_y) {
    Mesh out;
    Vec3 d = light_dir.normalized();
    // 对每个顶点：沿 d 方向投影到 y=ground_y
    for (int i = 0; i < src.vertex_count(); i++) {
        Vec3 p = src.verts[i].pos;
        double t = (ground_y - p.y) / (d.y + 1e-9);
        Vec3 sp = p + d * t;
        sp.y = ground_y;
        out.verts.push(Vertex(sp, Vec3(0, 1, 0)));
    }
    for (int f = 0; f < src.face_count(); f++) {
        out.faces.push(src.faces[f]);
        out.face_mat.push(-1);
    }
    return out;
}

Mesh build_shadow_volume(const Mesh& src, const Vec3& light_dir, double extrude) {
    Mesh out;
    Vec3 d = light_dir.normalized() * extrude;
    // 复制原顶点 + 挤出顶点
    for (int i = 0; i < src.vertex_count(); i++)
        out.verts.push(src.verts[i]);
    int base = out.verts.size();
    for (int i = 0; i < src.vertex_count(); i++)
        out.verts.push(Vertex(src.verts[i].pos + d, Vec3(0, 0, 0)));
    // 侧面：每条边一个四边形
    for (int f = 0; f < src.face_count(); f++) {
        for (int e = 0; e < 3; e++) {
            int a = src.faces[f][e];
            int b = src.faces[f][(e + 1) % 3];
            out.faces.push(Face(a, b, base + b));
            out.faces.push(Face(a, base + b, base + a));
            out.face_mat.push(-1);
            out.face_mat.push(-1);
        }
    }
    return out;
}

void ShadowMap::alloc(int W, int H) {
    free();
    w = W; h = H;
    depth = new float[(size_t)w * h];
    clear();
}

void ShadowMap::free() {
    if (depth) { delete[] depth; depth = 0; }
}

void ShadowMap::clear() {
    for (int i = 0; i < w * h; i++) depth[i] = 1e30f;
}

bool ShadowMap::is_occluded(const Vec3& world_p) const {
    if (!depth) return false;
    Vec4 clip = light_vp * Vec4(world_p, 1.0);
    if (clip.w <= 0) return false;
    double ndc_z = clip.z / clip.w;
    double ndc_x = clip.x / clip.w;
    double ndc_y = clip.y / clip.w;
    int px = (int)((ndc_x * 0.5 + 0.5) * w);
    int py = (int)((ndc_y * 0.5 + 0.5) * h);
    if (px < 0 || px >= w || py < 0 || py >= h) return false;
    float stored = depth[py * w + px];
    return ndc_z > stored + 0.001;
}

void render_shadow_map(ShadowMap& sm, const Mesh& m, const Mat4& model,
                       const Vec3& light_pos, double ortho_size) {
    sm.clear();
    Mat4 view = mat4_look_at(light_pos, Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = mat4_ortho_center(ortho_size, ortho_size, 0.1, 50);
    sm.light_vp = proj * view * model;
    // 简化：只写包围盒深度（真正的阴影贴图需要完整光栅化）
    for (int i = 0; i < m.vertex_count(); i++) {
        Vec4 clip = sm.light_vp * Vec4(m.verts[i].pos, 1);
        if (clip.w <= 0) continue;
        double nz = clip.z / clip.w;
        double nx = clip.x / clip.w;
        double ny = clip.y / clip.w;
        int px = (int)((nx * 0.5 + 0.5) * sm.w);
        int py = (int)((ny * 0.5 + 0.5) * sm.h);
        if (px < 0 || px >= sm.w || py < 0 || py >= sm.h) continue;
        float z = (float)((nz + 1.0) * 0.5);
        if (z < sm.depth[py * sm.w + px]) sm.depth[py * sm.w + px] = z;
    }
}

int shadows_self_test() {
    int fail = 0;
    // 投影阴影：立方体投影到地面 y=0
    {
        Mesh cube;
        make_cube(cube, 1.0);
        cube.translate(Vec3(0, 1, 0));
        Mesh sh = project_shadow_mesh(cube, Vec3(0, -1, 0), 0.0);
        // 投影后所有顶点 y 应为 0
        for (int i = 0; i < sh.vertex_count(); i++)
            if (sh.verts[i].pos.y > 1e-6) { fail++; break; }
    }
    // 阴影体：挤出后顶点数翻倍
    {
        Mesh cube;
        make_cube(cube, 1.0);
        Mesh sv = build_shadow_volume(cube, Vec3(0, -1, 0), 2.0);
        if (sv.vertex_count() != 16) fail++;
    }
    // 阴影贴图
    {
        ShadowMap sm;
        sm.alloc(16, 16);
        Mesh cube;
        make_cube(cube, 1.0);
        render_shadow_map(sm, cube, Mat4::identity(), Vec3(0, 5, 0), 2.0);
        // 中心像素应被写
        if (sm.depth[8 * 16 + 8] > 1e29) fail++;
        sm.free();
    }
    return fail;
}

} // namespace gfx3d
} // namespace nefu
