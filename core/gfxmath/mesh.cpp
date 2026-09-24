// nefuOS gfxmath —— 网格实现 + 自测
#include "gfxmath/mesh.h"
#include <cmath>
#include <cstdio>

namespace nefu {
namespace gfx {

Mesh::Mesh() {}

void Mesh::clear() { verts.clear(); tris.clear(); }

int Mesh::add_vertex(const Vec3& p) {
    verts.push_back(Vertex(p));
    return (int)verts.size() - 1;
}

int Mesh::add_triangle(int a, int b, int c) {
    tris.push_back(a); tris.push_back(b); tris.push_back(c);
    return (int)tris.size() / 3 - 1;
}

void Mesh::get_triangle(int i, int& a, int& b, int& c) const {
    a = tris[i * 3]; b = tris[i * 3 + 1]; c = tris[i * 3 + 2];
}

void Mesh::compute_normals() {
    // 先清零
    for (size_t i = 0; i < verts.size(); i++) verts[i].normal = Vec3();
    // 每个面法线累加到顶点
    for (size_t i = 0; i < tris.size(); i += 3) {
        const Vec3& pa = verts[tris[i]].pos;
        const Vec3& pb = verts[tris[i + 1]].pos;
        const Vec3& pc = verts[tris[i + 2]].pos;
        Vec3 n = (pb - pa).cross(pc - pa);
        verts[tris[i]].normal += n;
        verts[tris[i + 1]].normal += n;
        verts[tris[i + 2]].normal += n;
    }
    // 归一化
    for (size_t i = 0; i < verts.size(); i++) verts[i].normal = verts[i].normal.normalized();
}

Mesh Mesh::cube(float size) {
    Mesh m;
    float h = size * 0.5f;
    // 8 个顶点
    int idx[8];
    idx[0] = m.add_vertex(Vec3(-h, -h, -h));
    idx[1] = m.add_vertex(Vec3(h, -h, -h));
    idx[2] = m.add_vertex(Vec3(h, h, -h));
    idx[3] = m.add_vertex(Vec3(-h, h, -h));
    idx[4] = m.add_vertex(Vec3(-h, -h, h));
    idx[5] = m.add_vertex(Vec3(h, -h, h));
    idx[6] = m.add_vertex(Vec3(h, h, h));
    idx[7] = m.add_vertex(Vec3(-h, h, h));
    // 6 个面 x 2 三角形
    m.add_triangle(idx[0], idx[1], idx[2]); m.add_triangle(idx[0], idx[2], idx[3]);  // -Z
    m.add_triangle(idx[4], idx[6], idx[5]); m.add_triangle(idx[4], idx[7], idx[6]);  // +Z
    m.add_triangle(idx[0], idx[5], idx[1]); m.add_triangle(idx[0], idx[4], idx[5]);  // -Y
    m.add_triangle(idx[3], idx[2], idx[6]); m.add_triangle(idx[3], idx[6], idx[7]);  // +Y
    m.add_triangle(idx[0], idx[3], idx[7]); m.add_triangle(idx[0], idx[7], idx[4]);  // -X
    m.add_triangle(idx[1], idx[5], idx[6]); m.add_triangle(idx[1], idx[6], idx[2]);  // +X
    m.compute_normals();
    return m;
}

Mesh Mesh::sphere(float radius, int stacks, int slices) {
    Mesh m;
    // 顶点网格（纬度 stacks 行 + 两极）
    std::vector<int> row0, row1;
    for (int st = 0; st <= stacks; st++) {
        double phi = 3.14159265358979 * st / stacks;   // 0..pi
        double y = radius * std::cos(phi);
        double r = radius * std::sin(phi);
        std::vector<int>& cur = (st == 0) ? row0 : row1;
        if (st == 0) {
            m.add_vertex(Vec3(0, radius, 0));
            continue;
        }
        for (int sl = 0; sl < slices; sl++) {
            double theta = 2 * 3.14159265358979 * sl / slices;
            double x = r * std::cos(theta);
            double z = r * std::sin(theta);
            cur.push_back(m.add_vertex(Vec3(x, y, z)));
        }
        if (st == 1) {
            // 连接极点与第一环
            for (int sl = 0; sl < slices; sl++) {
                int a = 0;
                int b = row1[sl];
                int c = row1[(sl + 1) % slices];
                m.add_triangle(a, c, b);
            }
            row0 = row1; row1.clear();
        } else if (st == stacks) {
            // 连接末环与下极点
            for (int sl = 0; sl < slices; sl++) {
                int a = 1 + (stacks - 1) * slices;   // 末环首顶点索引起点
                (void)a;
                break;
            }
            int pole = m.add_vertex(Vec3(0, -radius, 0));
            for (int sl = 0; sl < slices; sl++) {
                int b = row0[sl];
                int c = row0[(sl + 1) % slices];
                m.add_triangle(pole, c, b);
            }
        } else {
            // 中间环：两环间两三角形
            for (int sl = 0; sl < slices; sl++) {
                int a0 = row0[sl], a1 = row0[(sl + 1) % slices];
                int b0 = row1[sl], b1 = row1[(sl + 1) % slices];
                m.add_triangle(a0, a1, b1);
                m.add_triangle(a0, b1, b0);
            }
            row0 = row1; row1.clear();
        }
    }
    m.compute_normals();
    return m;
}

Mesh Mesh::plane(float w, float h, int gw, int gh) {
    Mesh m;
    // 顶点网格：每行 gw+1 个，共 gh+1 行
    for (int y = 0; y <= gh; y++) {
        float py = h * (0.5f - (float)y / gh);
        for (int x = 0; x <= gw; x++) {
            float px = w * ((float)x / gw - 0.5f);
            m.add_vertex(Vec3(px, py, 0));
        }
    }
    // 行间三角形：row 是上一行首顶点索引
    for (int y = 0; y < gh; y++) {
        int top = y * (gw + 1);
        int bot = (y + 1) * (gw + 1);
        for (int x = 0; x < gw; x++) {
            m.add_triangle(top + x, top + x + 1, bot + x + 1);
            m.add_triangle(top + x, bot + x + 1, bot + x);
        }
    }
    m.compute_normals();
    return m;
}

void Mesh::transform(const Mat4& m4, const Mat4& normal_m) {
    for (size_t i = 0; i < verts.size(); i++) {
        verts[i].pos = m4.transform(verts[i].pos);
        verts[i].normal = normal_m.transform_dir(verts[i].normal).normalized();
    }
}

void Mesh::bounding_box(Vec3& minb, Vec3& maxb) const {
    if (verts.empty()) { minb = Vec3(); maxb = Vec3(); return; }
    minb = maxb = verts[0].pos;
    for (size_t i = 1; i < verts.size(); i++) {
        const Vec3& p = verts[i].pos;
        if (p.x < minb.x) minb.x = p.x;
        if (p.y < minb.y) minb.y = p.y;
        if (p.z < minb.z) minb.z = p.z;
        if (p.x > maxb.x) maxb.x = p.x;
        if (p.y > maxb.y) maxb.y = p.y;
        if (p.z > maxb.z) maxb.z = p.z;
    }
}

// ---- self test ----
int Mesh::self_test() {
    int fails = 0;
    // 1. 基础构建
    {
        Mesh m;
        int a = m.add_vertex(Vec3(0, 0, 0));
        int b = m.add_vertex(Vec3(1, 0, 0));
        int c = m.add_vertex(Vec3(0, 1, 0));
        m.add_triangle(a, b, c);
        if (m.vertex_count() != 3) fails++;
        if (m.triangle_count() != 1) fails++;
        int ta, tb, tc;
        m.get_triangle(0, ta, tb, tc);
        if (ta != a || tb != b || tc != c) fails++;
    }
    // 2. 法线计算
    {
        Mesh m;
        m.add_triangle(m.add_vertex(Vec3(0, 0, 0)),
                       m.add_vertex(Vec3(1, 0, 0)),
                       m.add_vertex(Vec3(0, 1, 0)));
        m.compute_normals();
        // 面朝 +Z
        if (std::abs(m.vertex(0).normal.z - 1) > 1e-9) fails++;
    }
    // 3. 立方体
    {
        Mesh c = Mesh::cube(2);
        if (c.vertex_count() != 8) fails++;
        if (c.triangle_count() != 12) fails++;
        Vec3 mn, mx;
        c.bounding_box(mn, mx);
        if (mn.x != -1 || mx.x != 1 || mn.y != -1 || mx.y != 1) fails++;
    }
    // 4. 球体
    {
        Mesh s = Mesh::sphere(1, 8, 12);
        if (s.vertex_count() < 40) fails++;
        if (s.triangle_count() < 80) fails++;
        Vec3 mn, mx;
        s.bounding_box(mn, mx);
        if (mx.x > 1.001 || mn.x < -1.001) fails++;   // 半径约 1
    }
    // 5. 平面
    {
        Mesh p = Mesh::plane(2, 2, 4, 4);
        if (p.vertex_count() != 25) fails++;          // 5x5
        if (p.triangle_count() != 32) fails++;        // 4x4 x 2
    }
    // 6. 变换
    {
        Mesh m;
        m.add_triangle(m.add_vertex(Vec3(0, 0, 0)), m.add_vertex(Vec3(1, 0, 0)), m.add_vertex(Vec3(0, 1, 0)));
        m.compute_normals();
        Mat4 T = Mat4::translation(10, 0, 0);
        m.transform(T, T);
        if (std::abs(m.vertex(0).pos.x - 10) > 1e-9) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
