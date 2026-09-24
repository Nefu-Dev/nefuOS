// nefuOS gfxmath —— 投影 proj 与渲染器 render
// 教学版：把 3D 网格投影到 2D 屏幕坐标（顶点着色管线教学版），
// 以及一个极简软件渲染器（画线框网格）。
#pragma once
#include <vector>
#include <string>
#include "gfxmath/vec3.h"
#include "gfxmath/mat4.h"
#include "gfxmath/mesh.h"

namespace nefu {
namespace gfx {

// 屏幕坐标点
struct ScreenPoint {
    int x, y;
    double depth;   // 深度（越小越近，用于画家算法排序）
    ScreenPoint() : x(0), y(0), depth(0) {}
    ScreenPoint(int X, int Y, double D) : x(X), y(Y), depth(D) {}
};

// 投影器：把 3D 顶点经 VP 矩阵变换到 NDC，再映射到屏幕像素
class Projector {
public:
    // 构造：屏幕宽高
    Projector(int width, int height);

    // 设置视图投影矩阵（相机 view_proj_matrix()）
    void set_view_proj(const Mat4& vp);
    // 把 3D 点投影到屏幕（返回是否在视锥内；不在则 out 无效）
    bool project(const Vec3& world, ScreenPoint& out) const;
    // 批量投影顶点
    std::vector<ScreenPoint> project_many(const std::vector<Vec3>& pts) const;

    // 投影整个网格的三角形（线框渲染用），返回屏幕三角形列表
    // 每 3 个 ScreenPoint 一组
    std::vector<ScreenPoint> project_mesh(const Mesh& mesh) const;

    int width() const { return w; }
    int height() const { return h; }

    // ---- self test ----
    static int self_test();

private:
    int w, h;
    Mat4 vp;
};

// 极简软件渲染器：把投影后的三角形画到颜色缓冲（ASCII 或 RGB）
class Renderer {
public:
    Renderer(int width, int height);

    // 画一条线框三角形（Bresenham 画线）
    void draw_line(int x0, int y0, int x1, int y1, char mark);
    // 画一个投影三角形（投影器输出）
    void draw_triangle(const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c);
    // 输出文本画面
    std::string frame_to_text() const;
    // 清空画面
    void clear();

    // 用 painter 算法（按深度排序三角形）渲染线框网格
    std::string render_wireframe(const Mesh& mesh, const Mat4& vp);

    int width() const { return w; }
    int height() const { return h; }

    // ---- self test ----
    static int self_test();

private:
    int w, h;
    std::vector<std::vector<char> > buf;
};

} // namespace gfx
} // namespace nefu
