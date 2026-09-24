// ============================================================================
// nefuOS 3D 图形库 —— scene: 场景图
// ----------------------------------------------------------------------------
//   - SceneNode：本地变换 + 子节点列表 + 可选网格
//   - 层级遍历：前序遍历，矩阵栈 push/pop
//   - 包围体：递归合并子节点 AABB
//   - 视锥剔除：遍历前用 Frustum 快速丢弃不可见子树
//   - 渲染入口：把场景提交给 Rasterizer，逐三角形走完整管线
// ============================================================================
#pragma once
#include "math3d.h"
#include "mesh.h"
#include "raster.h"
#include "light.h"

namespace nefu {
namespace gfx3d {

// 着色模式
enum ShadeMode {
    SHADE_WIREFRAME = 0,
    SHADE_FLAT,
    SHADE_GOURAUD,
    SHADE_PHONG
};

// 场景节点
struct SceneNode {
    String name;
    Mat4 local;               // 本地变换（相对父节点）
    List<SceneNode*> children;
    Mesh* mesh;               // 可渲染网格（可为空）
    ShadeMaterial material;
    bool visible;
    AABB world_bounds;        // 世界坐标包围盒（遍历时更新）

    SceneNode() : mesh(0), visible(true) {
        local = Mat4::identity();
    }
    ~SceneNode() {
        for (int i = 0; i < children.size(); i++) delete children[i];
    }

    SceneNode* add(SceneNode* child) { children.push(child); return child; }
    SceneNode* add_mesh(Mesh* m) {
        SceneNode* n = new SceneNode();
        n->mesh = m;
        children.push(n);
        return n;
    }

    // 递归计算世界包围盒（model = 父矩阵 * local）
    void update_bounds(const Mat4& parent);

    // 计算世界包围球
    Sphere world_sphere() const;
};

// 场景序列化：把节点树写成文本（名称 + 局部平移/旋转/缩放）
// 返回字符串（调用方 delete[]）。data 为输出缓冲。
void scene_serialize(const SceneNode& root, String& out);
// 从文本读回（简化版：只建空节点，不挂载网格）
bool scene_deserialize(const char* text, SceneNode& root);

// 场景：根节点 + 相机 + 光照环境
struct Scene {
    SceneNode root;
    LightingEnv lights;
    // 相机
    Vec3 eye, target, up;
    double fovy, aspect, znear, zfar;

    Scene() : eye(0, 0, 5), target(0, 0, 0), up(0, 1, 0),
              fovy(1.0), aspect(4.0 / 3.0), znear(0.1), zfar(100) {}

    Mat4 view_matrix() const { return mat4_look_at(eye, target, up); }
    Mat4 proj_matrix() const { return mat4_perspective(fovy, aspect, znear, zfar); }

    // 遍历场景：对每个可见网格节点调用 draw
    void render(Rasterizer& rast, ShadeMode mode);
};

// self test
int scene_self_test();

} // namespace gfx3d
} // namespace nefu
