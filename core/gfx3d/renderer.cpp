// ============================================================================
// nefuOS 3D 图形库 —— renderer 实现
// ============================================================================
#include "renderer.h"
#include "ppm.h"

namespace nefu {
namespace gfx3d {

void Renderer::draw_mesh(const Mesh& m, const Mat4& model, ShadeMode mode) {
    Mat4 vp = proj * view;
    Mat4 mvp = vp * model;

    PhongShaderUser user;
    user.env = &env;
    user.mat = &mat;

    if (mode == SHADE_WIREFRAME) {
        rast.backface_cull = false;
        rast.frag_shader = 0;
        for (int f = 0; f < m.face_count(); f++) {
            for (int e = 0; e < 3; e++) {
                int a = m.faces[f][e];
                int b = m.faces[f][(e + 1) % 3];
                Vec4 pa = mvp * Vec4(m.verts[a].pos, 1);
                Vec4 pb = mvp * Vec4(m.verts[b].pos, 1);
                if (pa.w < 0.01 || pb.w < 0.01) continue;
                double ax = pa.x / pa.w, ay = pa.y / pa.w;
                double bx = pb.x / pb.w, by = pb.y / pb.w;
                int x0 = (int)((ax * 0.5 + 0.5) * rast.w);
                int y0 = (1 - (ay * 0.5 + 0.5)) * rast.h;
                int x1 = (int)((bx * 0.5 + 0.5) * rast.w);
                int y1 = (1 - (by * 0.5 + 0.5)) * rast.h;
                rast.draw_line_screen(x0, y0, x1, y1, 0xFF66CCFF);
            }
        }
        return;
    }

    if (mode == SHADE_PHONG) {
        rast.frag_shader = phong_fragment_shader;
        rast.frag_user = &user;
    } else {
        rast.frag_shader = 0;
    }
    rast.backface_cull = true;

    for (int f = 0; f < m.face_count(); f++) {
        Rasterizer::ClipVert cv[3];
        for (int k = 0; k < 3; k++) {
            const Vertex& v = m.verts[m.faces[f][k]];
            cv[k].clip   = mvp * Vec4(v.pos, 1);
            cv[k].world  = (model * Vec4(v.pos, 1)).xyz();
            cv[k].normal = (model * Vec4(v.normal, 0)).xyz().normalized();
            cv[k].uv     = v.uv;
            cv[k].color  = v.color;
        }
        rast.draw_triangle(cv[0], cv[1], cv[2]);
    }
}

// ============================================================================
// self test
// ============================================================================
int renderer_self_test() {
    int fail = 0;
    const int W = 32, H = 32;
    uint32_t* pix = new uint32_t[(size_t)W * H];
    nefu::gfxlib::Buffer buf = { pix, W, H };
    Renderer r;
    r.attach(buf);
    r.clear();
    r.set_camera(Vec3(0, 0, 3), Vec3(0, 0, 0));
    r.set_light(Vec3(0.3, 0.5, -0.8));

    Mesh cube;
    make_cube(cube, 1.0);
    r.draw_mesh(cube, Mat4::identity(), SHADE_PHONG);

    // 中心应被渲染（非背景色）
    uint32_t c = pix[(H/2) * W + W/2];
    if ((c & 0xFFFFFF) == 0x101418) fail++;   // 没画上

    // PPM 写出
    uint8_t ppm[256 * 1024];
    int n = r.write_ppm(ppm, sizeof(ppm));
    if (n <= 0) fail++;

    r.detach();
    delete[] pix;
    return fail;
}

} // namespace gfx3d
} // namespace nefu
