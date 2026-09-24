// ============================================================================
// nefuOS 3D 模型查看器 —— gfx3dview
// ----------------------------------------------------------------------------
// 一个完整的软件渲染 3D 查看器：
//   - 加载 OBJ 模型，或选择内置图元（立方体/球体/圆环/茶壶）
//   - 方向键：绕 Y / X 轴旋转相机；+/-：缩放
//   - 数字键 1..4：切换着色模式（线框/平面/Gouraud/Phong）
//   - L：开关光照；空格：自动旋转；ESC：关闭
//   - 用 gfxlib 离屏 Buffer 做软件渲染，每帧光栅化整个场景后 blit 到窗口
// ============================================================================
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../gfxlib/gfxlib_all.h"
#include "gfx3d/gfx3d_all.h"

namespace nefu {
namespace gfx3dview {

using namespace nefu::gfx3d;

const int VW = 400, VH = 300;   // 离屏渲染分辨率
const int WIN_W = VW + 20, WIN_H = VH + 60;

// 内置图元编号
enum Prim {
    PRIM_CUBE = 0, PRIM_SPHERE, PRIM_TORUS, PRIM_TEAPOT, PRIM_CYLINDER,
    PRIM_KNOT, PRIM_GRID, PRIM_COUNT
};
const char* PRIM_NAMES[PRIM_COUNT] = {
    "Cube", "Sphere", "Torus", "Teapot", "Cylinder", "Knot", "Grid"
};

struct ViewState {
    uint32_t* buf;          // 离屏像素
    Mesh* mesh;             // 当前网格
    Prim  prim;
    ShadeMode mode;
    bool  lighting;
    double rot_y, rot_x;    // 相机角度（弧度）
    double dist;            // 相机距离
    uint32_t tick;
    bool  auto_rotate;

    ViewState() : buf(0), mesh(0), prim(PRIM_TEAPOT), mode(SHADE_PHONG),
                  lighting(true), rot_y(0.6), rot_x(0.3), dist(4.0),
                  tick(0), auto_rotate(true) {}

    void load_prim(Prim p) {
        delete mesh;
        mesh = new Mesh();
        switch (p) {
            case PRIM_CUBE:     make_cube(*mesh, 1.2); break;
            case PRIM_SPHERE:   make_sphere_uv(*mesh, 1.0, 24, 32); break;
            case PRIM_TORUS:    make_torus(*mesh, 0.7, 0.28, 24, 16); break;
            case PRIM_TEAPOT:   make_teapot(*mesh, 0.9, 3); break;
            case PRIM_CYLINDER: make_cylinder(*mesh, 0.5, 1.2, 24); break;
            case PRIM_KNOT:     make_torus_knot(*mesh, 1.0, 0.3, 2, 3, 40); break;
            case PRIM_GRID:     make_grid(*mesh, 10, 4.0); break;
            default: make_cube(*mesh, 1.0); break;
        }
        mesh->compute_smooth_normals();
        prim = p;
    }

    // 渲染一帧到 buf
    void render() {
        gfxlib::Buffer target = { buf, VW, VH };
        Rasterizer rast;
        rast.attach(target);
        rast.clear(0xFF101418);

        // 相机
        Vec3 eye(0, 0, dist);
        Vec3 center(0, 0, 0);
        Vec3 up(0, 1, 0);
        Mat4 view = mat4_look_at(eye, center, up);
        Mat4 proj = mat4_perspective(deg2rad(60.0), (double)VW / VH, 0.1, 100);
        Mat4 vp = proj * view;

        // 模型矩阵：旋转
        Mat4 model = mat4_rotate_y(rot_y) * mat4_rotate_x(rot_x);

        // 光照
        LightingEnv env;
        env.eye_pos = eye;
        Light l;
        l.type = LIGHT_DIRECTIONAL;
        l.direction = Vec3(0.5, 0.8, -0.6).normalized();
        l.color = Vec3(1, 1, 1);
        l.intensity = lighting ? 1.0 : 0.0;
        env.add_light(l);
        env.global_ambient = Vec3(0.25, 0.25, 0.28);

        ShadeMaterial mat;
        mat.diffuse = Vec3(0.6, 0.4, 0.2);
        mat.specular = Vec3(0.6, 0.6, 0.6);
        mat.shininess = 40;

        PhongShaderUser user;
        user.env = &env;
        user.mat = &mat;
        if (mode == SHADE_PHONG && lighting) {
            rast.frag_shader = phong_fragment_shader;
            rast.frag_user = &user;
        } else {
            rast.frag_shader = 0;
        }
        rast.backface_cull = true;

        // 逐三角形
        Mat4 mvp = vp * model;
        if (mode == SHADE_WIREFRAME) {
            rast.backface_cull = false;
            for (int f = 0; f < mesh->face_count(); f++) {
                for (int e = 0; e < 3; e++) {
                    int a = mesh->faces[f][e];
                    int b = mesh->faces[f][(e + 1) % 3];
                    Vec4 pa = mvp * Vec4(mesh->verts[a].pos, 1);
                    Vec4 pb = mvp * Vec4(mesh->verts[b].pos, 1);
                    if (pa.w < 0.01 || pb.w < 0.01) continue;
                    double ax = pa.x / pa.w, ay = pa.y / pa.w;
                    double bx = pb.x / pb.w, by = pb.y / pb.w;
                    int x0 = (int)((ax * 0.5 + 0.5) * VW);
                    int y0 = (1 - (ay * 0.5 + 0.5)) * VH;
                    int x1 = (int)((bx * 0.5 + 0.5) * VW);
                    int y1 = (1 - (by * 0.5 + 0.5)) * VH;
                    rast.draw_line_screen(x0, y0, x1, y1, 0xFF66CCFF);
                }
            }
        } else {
            for (int f = 0; f < mesh->face_count(); f++) {
                Rasterizer::ClipVert cv[3];
                for (int k = 0; k < 3; k++) {
                    const Vertex& v = mesh->verts[mesh->faces[f][k]];
                    cv[k].clip   = mvp * Vec4(v.pos, 1);
                    cv[k].world  = (model * Vec4(v.pos, 1)).xyz();
                    cv[k].normal = (model * Vec4(v.normal, 0)).xyz().normalized();
                    cv[k].uv     = v.uv;
                    cv[k].color  = v.color;
                }
                rast.draw_triangle(cv[0], cv[1], cv[2]);
            }
        }
        rast.detach();
    }

    void paint(Surface& s) {
        // blit 离屏 buffer -> 窗口
        for (int y = 0; y < VH && y < (int)s.height; y++) {
            const uint32_t* row = buf + (size_t)y * VW;
            for (int x = 0; x < VW && x < (int)s.width; x++)
                s.setpx(x, y, row[x]);
        }
        char cap[160];
        const char* mname = "?";
        switch (mode) {
            case SHADE_WIREFRAME: mname = "Wire"; break;
            case SHADE_FLAT:      mname = "Flat"; break;
            case SHADE_GOURAUD:   mname = "Gouraud"; break;
            case SHADE_PHONG:     mname = "Phong"; break;
        }
        ksprintf(cap, sizeof(cap),
                 "gfx3dview: %s  V:%d F:%d  [%s]  1-7 prim  arrows rot  +/- zoom  L light",
                 PRIM_NAMES[prim], mesh->vertex_count(), mesh->face_count(), mname);
        gfx::text(s, 4, VH + 6, cap, 0xFFDDDDDD, 0xFF101418);
    }
};

} // namespace gfx3dview

using namespace gfx3dview;

static ViewState* vs_of(Window* w) { return (ViewState*)w->userdata; }

static void view_paint(Window* w) { vs_of(w)->paint(w->back); }

static void view_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    ViewState* v = vs_of(w);
    switch (e->keycode) {
        case KEY_LEFT:  v->rot_y -= 0.1; break;
        case KEY_RIGHT: v->rot_y += 0.1; break;
        case KEY_UP:    v->rot_x -= 0.1; break;
        case KEY_DOWN:  v->rot_x += 0.1; break;
        case KEY_ESC:   g_wm->close_window(w); return;
        default: break;
    }
    if (e->ascii == '+' || e->ascii == '=') v->dist -= 0.3;
    if (e->ascii == '-' || e->ascii == '_') v->dist += 0.3;
    if (v->dist < 1.5) v->dist = 1.5;
    if (v->dist > 20) v->dist = 20;
    if (e->ascii == 'l' || e->ascii == 'L') v->lighting = !v->lighting;
    if (e->keycode == KEY_SPACE) v->auto_rotate = !v->auto_rotate;
    if (e->ascii >= '1' && e->ascii <= '4') {
        v->mode = (ShadeMode)(e->ascii - '1');
    }
    if (e->ascii >= '5' && e->ascii <= '9') {
        int p = e->ascii - '1';
        if (p < PRIM_COUNT) v->load_prim((Prim)p);
    }
}

static void view_tick(Window* w) {
    ViewState* v = vs_of(w);
    if (!v) return;
    if (v->auto_rotate) v->rot_y += 0.01;
    v->tick++;
}

static void view_close(Window* w) {
    ViewState* v = (ViewState*)w->userdata;
    if (v) { delete v->mesh; delete[] v->buf; delete v; }
    w->userdata = 0;
}

void gfx3dview_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("gfx3d Viewer", x, y, WIN_W, WIN_H);
    if (!w) return;
    ViewState* v = new ViewState();
    v->buf = new uint32_t[(size_t)VW * VH];
    v->load_prim(PRIM_TEAPOT);
    w->userdata = v;
    w->on_paint = view_paint;
    w->on_key = view_key;
    w->on_tick = view_tick;
    w->on_close = view_close;
    g_wm->raise(w);
}

} // namespace nefu
