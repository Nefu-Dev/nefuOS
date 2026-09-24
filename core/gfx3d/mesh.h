// ============================================================================
// nefuOS 3D 图形库 —— mesh: 网格数据结构、文件解析、图元生成器
// ----------------------------------------------------------------------------
// 功能：
//   - Vertex：位置 / 法线 / 纹理坐标 / 颜色
//   - Face：三个顶点索引（三角面）
//   - Mesh：顶点列表 + 面列表 + 材质
//   - 法线计算：平滑法线（面积加权平均）/ 平面法线（每面一个）
//   - 文件格式：OBJ（v/vt/vn/f）、MTL 材质、PLY（ASCII/binary）、STL（ASCII/binary）
//   - 图元生成器：立方体 / UV 球 / 经纬球 / 圆柱 / 圆锥 / 圆环 / 平面 /
//                 四面体 / 八面体 / 二十面体 / 简化茶壶
//   - 网格变换：平移 / 旋转 / 缩放 / 合并
//
// 约束：不使用 STL 容器，用 nefu::List；不抛异常。
// ============================================================================
#pragma once
#include <stdint.h>
#include "math3d.h"
#include "../klib/klib.h"

namespace nefu {
namespace gfx3d {

// ============================================================================
// 顶点：位置 + 法线 + UV + 颜色（RGBA double，便于插值）
// ============================================================================
struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
    Vec4 color;     // r,g,b,a 在 [0,1]

    Vertex() : color(1, 1, 1, 1) {}
    explicit Vertex(const Vec3& p) : pos(p), color(1, 1, 1, 1) {}
    Vertex(const Vec3& p, const Vec3& n) : pos(p), normal(n), color(1, 1, 1, 1) {}
};

// ============================================================================
// 三角面：三个顶点索引（逆时针绕序为正面）
// ============================================================================
struct Face {
    int v[3];
    Face() { v[0] = v[1] = v[2] = 0; }
    Face(int a, int b, int c) { v[0] = a; v[1] = b; v[2] = c; }
    int operator[](int i) const { return v[i]; }
    int& operator[](int i) { return v[i]; }
};

// ============================================================================
// 材质（简化版：颜色 + 高光指数 + 自发光）
// ============================================================================
struct Material {
    String name;
    Vec3 ambient;
    Vec3 diffuse;
    Vec3 specular;
    double shininess;
    double alpha;

    Material()
        : ambient(0.1, 0.1, 0.1), diffuse(0.8, 0.8, 0.8),
          specular(0.3, 0.3, 0.3), shininess(32), alpha(1.0) {}
};

// ============================================================================
// 网格
// ============================================================================
struct Mesh {
    List<Vertex> verts;
    List<Face>   faces;
    List<Material> materials;
    // 每个面使用的材质索引（-1 表示默认）
    List<int>    face_mat;

    // 包围盒
    AABB bounds;

    int vertex_count() const { return verts.size(); }
    int face_count() const { return faces.size(); }

    void clear() {
        verts.clear();
        faces.clear();
        materials.clear();
        face_mat.clear();
        bounds = AABB();
    }

    // 添加一个顶点，返回索引
    int add_vertex(const Vertex& v) {
        verts.push(v);
        bounds.expand(v.pos);
        return verts.size() - 1;
    }

    int add_face(const Face& f, int mat = -1) {
        faces.push(f);
        face_mat.push(mat);
        return faces.size() - 1;
    }

    // ---- 法线计算 ----
    // 平滑法线：把每个面的法线按面积加权累加到三个顶点，最后归一
    void compute_smooth_normals();
    // 平面法线：直接写到每个顶点（共享顶点会被最后一个面覆盖）
    void compute_flat_normals();

    // ---- 变换 ----
    void translate(const Vec3& t);
    void rotate(const Mat4& rot);     // 用旋转部分
    void scale(const Vec3& s);
    void transform(const Mat4& m);    // 完整 4x4 变换（位置用 w=1，法线用逆转置 3x3）

    // 把 other 合并进来（追加顶点和面，索引偏移）
    void merge(const Mesh& other);
};

// ============================================================================
// 文件解析
// ============================================================================
// 解析 OBJ 文本（v / vt / vn / f，支持多边形三角扇化）。
// data 必须以 NUL 结尾。返回 true 成功。
bool obj_parse(const char* data, Mesh& out);

// 解析 MTL 文本：newmtl / Kd / Ks / Ka / Ns / d
bool mtl_parse(const char* data, List<Material>& out);

// 解析 PLY：自动识别 ASCII（"ply\nformat ascii"）与 binary 头。
// 只读 vertex x y z 与 face vertex_indices（>=3 时三角扇化）。
bool ply_parse(const char* data, int size, Mesh& out);

// 解析 STL：自动识别 ASCII（"solid ... facet normal"）与 binary。
bool stl_parse(const char* data, int size, Mesh& out);

// 按扩展名分派
bool mesh_load(const char* path_hint, const char* data, int size, Mesh& out);

// ---- 导出 ----
// 写 OBJ 文本到 buf，返回字节数
int mesh_write_obj(const Mesh& m, char* buf, int bufsz);
// 写二进制 STL 到 buf，返回字节数
int mesh_write_stl(const Mesh& m, uint8_t* buf, int bufsz);

// ============================================================================
// 图元生成器（写入 out，单位尺寸，中心在原点）
// ============================================================================
void make_cube(Mesh& out, double size = 1.0);
void make_plane(Mesh& out, double w = 1.0, double h = 1.0, int seg = 1);
void make_sphere_uv(Mesh& out, double r = 1.0, int rings = 16, int segs = 24);
void make_sphere_latlong(Mesh& out, double r = 1.0, int subdiv = 2);
void make_cylinder(Mesh& out, double r = 0.5, double h = 1.0, int segs = 24);
void make_cone(Mesh& out, double r = 0.5, double h = 1.0, int segs = 24);
void make_torus(Mesh& out, double R = 0.7, double r = 0.25, int major = 24, int minor = 12);
void make_tetrahedron(Mesh& out, double s = 1.0);
void make_octahedron(Mesh& out, double s = 1.0);
void make_icosahedron(Mesh& out, double s = 1.0);
// 简化茶壶：用一组硬编码的贝塞尔网格控制点（Utah teapot 子集）细分生成
void make_teapot(Mesh& out, double scale = 1.0, int subdiv = 3);

// 更多图元
void make_torus_knot(Mesh& out, double R = 1.0, double r = 0.3, int p = 2, int q = 3, int seg = 32);
void make_sphere_shell(Mesh& out, double radius = 1.0, double thickness = 0.1, int rings = 16, int segs = 24);
void make_rounded_box(Mesh& out, double w = 1, double h = 1, double d = 1, double r = 0.2, int seg = 4);
void make_grid(Mesh& out, int cells = 10, double size = 1.0);

// ---- 网格后处理 ----
// 焊接位置重合的顶点（合并距离 < eps），返回合并掉的顶点数
int  mesh_weld_vertices(Mesh& m, double eps = 1e-4);
// 车削曲面：把轮廓点 (x=半径, y=高度) 绕 Y 轴旋转 segs 次
void make_lathe(Mesh& out, const Vec2* profile, int n_profile, int segs = 24);
// 螺旋线管：半径 R，管半径 r，圈数 turns
void make_helix(Mesh& out, double R = 1.0, double r = 0.1, double turns = 3.0,
                int path_segs = 64, int tube_segs = 8);

// ============================================================================
// self test
// ============================================================================
int mesh_self_test();

} // namespace gfx3d
} // namespace nefu
