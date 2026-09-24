// ============================================================================
// nefuOS 3D 图形库 —— renderer: 高层渲染便利类
// ----------------------------------------------------------------------------
// 把整个软件渲染管线打包成一个对象：
//   Renderer r;
//   r.attach(buffer);
//   r.set_camera(eye, target, fov);
//   r.set_light(dir, color);
//   r.draw_mesh(mesh, model_matrix, shade_mode);
// 供 gfx3dview 应用与 render3d 终端命令共用。
// ============================================================================
#pragma once
#include "math3d.h"
#include "mesh.h"
#include "raster.h"
#include "light.h"
#include "scene.h"
#include "ppm.h"

namespace nefu {
namespace gfx3d {

struct Renderer {
    Rasterizer rast;
    LightingEnv env;
    ShadeMaterial mat;
    Mat4 view, proj;
    double aspect;

    Renderer() : aspect(4.0 / 3.0) {
        env.global_ambient = Vec3(0.2, 0.2, 0.22);
        mat.diffuse = Vec3(0.7, 0.5, 0.3);
        mat.specular = Vec3(0.6, 0.6, 0.6);
        mat.shininess = 40;
    }

    // 绑定离屏 buffer
    void attach(nefu::gfxlib::Buffer buf) {
        rast.attach(buf);
        aspect = (double)buf.w / (double)buf.h;
    }
    void detach() { rast.detach(); }

    // 清屏
    void clear(uint32_t color = 0xFF101418) { rast.clear(color); }

    // 相机
    void set_camera(const Vec3& eye, const Vec3& target, double fovy_deg = 60,
                    double znear = 0.1, double zfar = 100) {
        view = mat4_look_at(eye, target, Vec3(0, 1, 0));
        proj = mat4_perspective(deg2rad(fovy_deg), aspect, znear, zfar);
        env.eye_pos = eye;
    }

    // 默认方向光
    void set_light(const Vec3& dir, const Vec3& color = Vec3(1, 1, 1),
                   double intensity = 1.0) {
        env.light_count = 0;
        Light l;
        l.type = LIGHT_DIRECTIONAL;
        l.direction = dir.normalized();
        l.color = color;
        l.intensity = intensity;
        env.add_light(l);
    }

    // 画一个网格（model = 世界变换）
    void draw_mesh(const Mesh& m, const Mat4& model, ShadeMode mode);

    // 把 buffer 写为 PPM 到内存
    int write_ppm(uint8_t* out, int outsz) {
        return ppm_write(rast.target.data, rast.w, rast.h, out, outsz);
    }
};

int renderer_self_test();

} // namespace gfx3d
} // namespace nefu
