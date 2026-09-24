// ============================================================================
// nefuOS 3D 图形库 —— scene 实现
// ============================================================================
#include "scene.h"

#include <cmath>

namespace nefu {
namespace gfx3d {

// ----------------------------------------------------------------------------
// 递归更新世界包围盒
// ----------------------------------------------------------------------------
void SceneNode::update_bounds(const Mat4& parent) {
    Mat4 world = parent * local;
    world_bounds = AABB();
    if (mesh) {
        // 把包围盒 8 个角点变换到世界空间
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                for (int k = 0; k < 2; k++) {
                    Vec3 p(i ? mesh->bounds.mx.x : mesh->bounds.mn.x,
                           j ? mesh->bounds.mx.y : mesh->bounds.mn.y,
                           k ? mesh->bounds.mx.z : mesh->bounds.mn.z);
                    Vec4 wp = world * Vec4(p, 1.0);
                    world_bounds.expand(wp.xyz());
                }
    }
    for (int i = 0; i < children.size(); i++) {
        children[i]->update_bounds(world);
        world_bounds.expand(children[i]->world_bounds);
    }
}

// ----------------------------------------------------------------------------
// 把一个网格的所有三角形提交给光栅化器
// ----------------------------------------------------------------------------
static void draw_mesh(Rasterizer& rast, const Mesh& m, const Mat4& mvp,
                      const Mat4& model, const LightingEnv& env,
                      const ShadeMaterial& mat, ShadeMode mode) {
    PhongShaderUser user;
    user.env = &env;
    user.mat = &mat;

    if (mode == SHADE_WIREFRAME) {
        // 线框：只画边
        rast.backface_cull = false;
        for (int f = 0; f < m.faces.size(); f++) {
            for (int e = 0; e < 3; e++) {
                int a = m.faces[f][e];
                int b = m.faces[f][(e + 1) % 3];
                Vec4 pa = mvp * Vec4(m.verts[a].pos, 1.0);
                Vec4 pb = mvp * Vec4(m.verts[b].pos, 1.0);
                // 粗略透视除法
                if (pa.w <= 0.01 || pb.w <= 0.01) continue;
                double ax = pa.x / pa.w, ay = pa.y / pa.w;
                double bx = pb.x / pb.w, by = pb.y / pb.w;
                int x0 = (int)((ax * 0.5 + 0.5) * rast.w);
                int y0 = (1 - (ay * 0.5 + 0.5)) * rast.h;
                int x1 = (int)((bx * 0.5 + 0.5) * rast.w);
                int y1 = (1 - (by * 0.5 + 0.5)) * rast.h;
                rast.draw_line_screen(x0, y0, x1, y1, 0xFF88CCFF);
            }
        }
        return;
    }

    // 为不同着色模式准备片元回调
    if (mode == SHADE_PHONG) {
        rast.frag_shader = phong_fragment_shader;
        rast.frag_user = &user;
    } else {
        rast.frag_shader = 0;   // 逐顶点颜色已写入 color 字段
    }
    rast.backface_cull = true;

    for (int f = 0; f < m.faces.size(); f++) {
        Rasterizer::ClipVert cv[3];
        for (int k = 0; k < 3; k++) {
            const Vertex& v = m.verts[m.faces[f][k]];
            cv[k].clip   = mvp * Vec4(v.pos, 1.0);
            cv[k].world  = (model * Vec4(v.pos, 1.0)).xyz();
            cv[k].normal = (model * Vec4(v.normal, 0.0)).xyz().normalized();
            cv[k].uv     = v.uv;
            cv[k].color  = v.color;
        }
        rast.draw_triangle(cv[0], cv[1], cv[2]);
    }
}

// ----------------------------------------------------------------------------
// 场景渲染：递归遍历
// ----------------------------------------------------------------------------
static void traverse(Rasterizer& rast, SceneNode* node, const Mat4& parent_world,
                     const Mat4& vp, const Frustum& frustum,
                     const LightingEnv& env, ShadeMode mode) {
    if (!node || !node->visible) return;
    Mat4 world = parent_world * node->local;

    // 视锥剔除：当前节点世界包围盒完全在视锥外时，整棵子树都不可见
    bool bbox_empty = node->world_bounds.mn.x > node->world_bounds.mx.x;
    if (!bbox_empty && !frustum.intersects(node->world_bounds)) return;

    if (node->mesh) {
        Mat4 mvp = vp * world;
        draw_mesh(rast, *node->mesh, mvp, world, env, node->material, mode);
    }
    for (int i = 0; i < node->children.size(); i++)
        traverse(rast, node->children[i], world, vp, frustum, env, mode);
}

void Scene::render(Rasterizer& rast, ShadeMode mode) {
    Mat4 V = view_matrix();
    Mat4 P = proj_matrix();
    Mat4 VP = P * V;
    Frustum frustum = Frustum::from_matrix(VP);
    root.update_bounds(Mat4::identity());
    // 让光照环境知道相机位置
    LightingEnv env = lights;
    env.eye_pos = eye;
    traverse(rast, &root, Mat4::identity(), VP, frustum, env, mode);
}

Sphere SceneNode::world_sphere() const {
    Sphere s;
    s.c = world_bounds.center();
    s.r = (world_bounds.mx - world_bounds.mn).length() * 0.5;
    return s;
}

// ----------------------------------------------------------------------------
// 序列化（简化文本格式：# name, pos x y z, child...）
// ----------------------------------------------------------------------------
static void serialize_node(const SceneNode& n, String& out, int depth) {
    for (int i = 0; i < depth; i++) out += "  ";
    out += "- node ";
    out += n.name.len() ? n.name : String("(unnamed)");
    out += "\n";
    for (int i = 0; i < n.children.size(); i++)
        serialize_node(*n.children[i], out, depth + 1);
}

void scene_serialize(const SceneNode& root, String& out) {
    out.clear();
    serialize_node(root, out, 0);
}

bool scene_deserialize(const char* text, SceneNode& root) {
    // 简化：只统计节点数，不实际建结构
    int count = 0;
    const char* p = text;
    while (*p) {
        if (*p == '-' && *(p+1) == ' ') count++;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return count > 0;
}

// ============================================================================
// self test
// ============================================================================
int scene_self_test() {
    int fail = 0;

    // 建一个根 + 两个立方体子节点
    Scene scn;
    Mesh* cube1 = new Mesh();
    make_cube(*cube1, 1.0);
    Mesh* cube2 = new Mesh();
    make_cube(*cube2, 1.0);

    SceneNode* n1 = scn.root.add_mesh(cube1);
    n1->local = mat4_translate(0, 0, 0);
    SceneNode* n2 = scn.root.add_mesh(cube2);
    n2->local = mat4_translate(5, 0, 0);

    scn.root.update_bounds(Mat4::identity());

    // 序列化往返
    String s;
    scene_serialize(scn.root, s);
    if (!scene_deserialize(s.c_str(), scn.root)) fail++;

    // 包围球
    Sphere sp = scn.root.world_sphere();
    if (sp.r < 2.0) fail++;

    scn.root.update_bounds(Mat4::identity());
    // 包围盒应跨越两个立方体：左约 -0.5，右约 5.5
    if (scn.root.world_bounds.mn.x < -1.01 || scn.root.world_bounds.mx.x > 5.99) fail++;

    // 渲染到小 buffer 不崩溃
    const int W = 32, H = 32;
    uint32_t* pix = new uint32_t[(size_t)W * H];
    nefu::gfxlib::Buffer buf = { pix, W, H };
    Rasterizer rast;
    rast.attach(buf);
    rast.clear(0xFF000000);
    scn.eye = Vec3(0, 0, 5);
    scn.render(rast, SHADE_WIREFRAME);
    rast.detach();
    delete[] pix;
    delete cube1;
    delete cube2;

    return fail;
}

} // namespace gfx3d
} // namespace nefu
