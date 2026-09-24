// nefuOS gfxmath —— 投影与渲染器实现 + 自测
#include "gfxmath/proj.h"
#include "gfxmath/camera.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace nefu {
namespace gfx {

Projector::Projector(int width, int height) : w(width), h(height) {
    vp = Mat4::identity();
}

void Projector::set_view_proj(const Mat4& vp_) { vp = vp_; }

bool Projector::project(const Vec3& world, ScreenPoint& out) const {
    // 变换到裁剪空间（齐次）
    double x = vp.m[0][0] * world.x + vp.m[0][1] * world.y + vp.m[0][2] * world.z + vp.m[0][3];
    double y = vp.m[1][0] * world.x + vp.m[1][1] * world.y + vp.m[1][2] * world.z + vp.m[1][3];
    double z = vp.m[2][0] * world.x + vp.m[2][1] * world.y + vp.m[2][2] * world.z + vp.m[2][3];
    double ww = vp.m[3][0] * world.x + vp.m[3][1] * world.y + vp.m[3][2] * world.z + vp.m[3][3];
    if (std::abs(ww) < 1e-12) return false;
    // 透视除法到 NDC [-1,1]
    double nx = x / ww, ny = y / ww, nz = z / ww;
    if (nx < -1 || nx > 1 || ny < -1 || ny > 1) return false;
    if (nz < -1 || nz > 1) return false;
    // 映射到屏幕
    out.x = (int)((nx + 1) * 0.5 * w);
    out.y = (int)((1 - ny) * 0.5 * h);
    out.depth = nz;
    if (out.x < 0) out.x = 0;
    if (out.y < 0) out.y = 0;
    if (out.x >= w) out.x = w - 1;
    if (out.y >= h) out.y = h - 1;
    return true;
}

std::vector<ScreenPoint> Projector::project_many(const std::vector<Vec3>& pts) const {
    std::vector<ScreenPoint> r;
    for (size_t i = 0; i < pts.size(); i++) {
        ScreenPoint sp;
        if (project(pts[i], sp)) r.push_back(sp);
    }
    return r;
}

std::vector<ScreenPoint> Projector::project_mesh(const Mesh& mesh) const {
    std::vector<ScreenPoint> r;
    for (int t = 0; t < mesh.triangle_count(); t++) {
        int a, b, c;
        mesh.get_triangle(t, a, b, c);
        ScreenPoint pa, pb, pc;
        bool ok = project(mesh.vertex(a).pos, pa) &&
                  project(mesh.vertex(b).pos, pb) &&
                  project(mesh.vertex(c).pos, pc);
        if (ok) {
            r.push_back(pa); r.push_back(pb); r.push_back(pc);
        }
    }
    return r;
}

// ---- self test ----
int Projector::self_test() {
    int fails = 0;
    // 1. 中心点投影到屏幕中心
    {
        Projector p(200, 100);
        // 相机在原点看 -Z，点 (0,0,-5) 应在屏幕中心
        Camera cam(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 1.0, 2.0, 0.1, 100);
        p.set_view_proj(cam.view_proj_matrix());
        ScreenPoint sp;
        if (!p.project(Vec3(0, 0, -5), sp)) fails++;
        if (std::abs(sp.x - 100) > 2) fails++;
        if (std::abs(sp.y - 50) > 2) fails++;
    }
    // 2. 侧面点投影不对称
    {
        Projector p(200, 100);
        Camera cam(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 1.0, 2.0, 0.1, 100);
        p.set_view_proj(cam.view_proj_matrix());
        ScreenPoint sp;
        if (!p.project(Vec3(1, 0, -5), sp)) fails++;
        if (sp.x <= 100) fails++;   // 右侧点应偏右
    }
    // 3. 投影立方体顶点数
    {
        Projector p(400, 300);
        Camera cam(Vec3(5, 4, 5), Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0, 1.3, 0.1, 100);
        p.set_view_proj(cam.view_proj_matrix());
        Mesh cube = Mesh::cube(2);
        std::vector<ScreenPoint> pts = p.project_mesh(cube);
        if (pts.empty()) fails++;   // 至少投影出一些三角形
        if (pts.size() % 3 != 0) fails++;
    }
    return fails;
}

Renderer::Renderer(int width, int height) : w(width), h(height) {
    buf.assign(h, std::vector<char>(w, ' '));
}

void Renderer::clear() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) buf[y][x] = ' ';
}

void Renderer::draw_line(int x0, int y0, int x1, int y1, char mark) {
    // Bresenham 画线（教学版：浮点斜率简化）
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy));
    if (steps == 0) {
        if (x0 >= 0 && y0 >= 0 && x0 < w && y0 < h) buf[y0][x0] = mark;
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (int)((long)dx * i / steps);
        int y = y0 + (int)((long)dy * i / steps);
        if (x >= 0 && y >= 0 && x < w && y < h) buf[y][x] = mark;
    }
}

void Renderer::draw_triangle(const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c) {
    draw_line(a.x, a.y, b.x, b.y, '*');
    draw_line(b.x, b.y, c.x, c.y, '*');
    draw_line(c.x, c.y, a.x, a.y, '*');
}

std::string Renderer::frame_to_text() const {
    std::string s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) s.push_back(buf[y][x]);
        if (y + 1 < h) s.push_back('\n');
    }
    return s;
}

std::string Renderer::render_wireframe(const Mesh& mesh, const Mat4& vp) {
    clear();
    Projector proj(w, h);
    proj.set_view_proj(vp);
    std::vector<ScreenPoint> tris = proj.project_mesh(mesh);
    // 画家算法：按深度（取三角形平均深度）从远到近绘制
    struct Tri {
        ScreenPoint a, b, c;
        double depth;
    };
    std::vector<Tri> ts;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        Tri t;
        t.a = tris[i]; t.b = tris[i + 1]; t.c = tris[i + 2];
        t.depth = (t.a.depth + t.b.depth + t.c.depth) / 3;
        ts.push_back(t);
    }
    // 深度大（远）先画
    std::sort(ts.begin(), ts.end(), [](const Tri& x, const Tri& y) { return x.depth > y.depth; });
    for (size_t i = 0; i < ts.size(); i++) draw_triangle(ts[i].a, ts[i].b, ts[i].c);
    return frame_to_text();
}

// ---- self test ----
int Renderer::self_test() {
    int fails = 0;
    // 1. 画线
    {
        Renderer r(10, 5);
        r.draw_line(0, 2, 9, 2, '#');
        std::string s = r.frame_to_text();
        if (s.find('#') == std::string::npos) fails++;
    }
    // 2. 清空
    {
        Renderer r(5, 5);
        r.draw_line(0, 0, 4, 4, 'x');
        r.clear();
        std::string s = r.frame_to_text();
        if (s.find('x') != std::string::npos) fails++;
    }
    // 3. 三角形画线
    {
        Renderer r(20, 20);
        ScreenPoint a(5, 15, 0), b(15, 15, 0), c(10, 5, 0);
        r.draw_triangle(a, b, c);
        std::string s = r.frame_to_text();
        if (s.find('*') == std::string::npos) fails++;
    }
    // 4. 渲染线框立方体
    {
        Renderer r(80, 50);
        Camera cam(Vec3(5, 4, 5), Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0, 1.6, 0.1, 100);
        Mesh cube = Mesh::cube(2);
        std::string s = r.render_wireframe(cube, cam.view_proj_matrix());
        if (s.find('*') == std::string::npos) fails++;
        if ((int)s.size() < 80 * 50) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
