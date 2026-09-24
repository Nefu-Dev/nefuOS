// ============================================================================
// gfx3d 独立编译入口：不依赖 nefuOS 的 VFS/WM，直接跑所有模块 self_test
// 编译命令（见任务说明）：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -O2 -I core tests\gfx3d_test_main.cpp \
//       core\gfx3d\*.cpp core\gfxlib\*.cpp core\klib\memory.cpp core\klib\string.cpp \
//       core\klib\printf.cpp -o gfx3d_test.exe
// ============================================================================
#include "gfx3d/gfx3d_all.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>

// host 桩：kalloc/kfree 由后端提供，独立测试里用 libc 顶替
namespace nefu {
void* kalloc(size_t sz) { return std::calloc(1, sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
// 调试输出桩（klib/printf.cpp 里的 klogf 会调到这里）
void  platform_dbg(const char*) {}
}

using namespace nefu::gfx3d;

int main() {
    int fail = 0;

    // ---- 各模块自检 ----
    int f_math  = math3d_self_test();
    int f_mesh  = mesh_self_test();
    int f_rast  = raster_self_test();
    int f_light = light_self_test();
    int f_scene = scene_self_test();
    int f_ppm   = ppm_self_test();
    int f_rend  = renderer_self_test();
    fail = f_math + f_mesh + f_rast + f_light + f_scene + f_ppm + f_rend;

    std::printf("math3d  self_test: %d failures\n", f_math);
    std::printf("mesh    self_test: %d failures\n", f_mesh);
    std::printf("raster  self_test: %d failures\n", f_rast);
    std::printf("light   self_test: %d failures\n", f_light);
    std::printf("scene   self_test: %d failures\n", f_scene);
    std::printf("ppm     self_test: %d failures\n", f_ppm);
    std::printf("renderer self_test: %d failures\n", f_rend);
    std::printf("--------------------------------\n");
    std::printf("TOTAL failures: %d\n", fail);

    // ---- 额外验证（任务书指定） ----
    // math3d: 矩阵乘单位矩阵 = 原矩阵
    {
        Mat4 T = mat4_translate(1, 2, 3);
        Mat4 I;
        Mat4 R = I * T;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (std::abs(R.m[i][j] - T.m[i][j]) > 1e-9) {
                    std::printf("FAIL: identity multiply\n");
                    fail++;
                }
    }
    // math3d: 旋转 90 度矩阵作用于 (1,0,0) 得 (0,1,0)
    {
        Mat4 R = mat4_rotate_z(deg2rad(90.0));
        Vec4 v = R * Vec4(1, 0, 0, 1);
        if (std::abs(v.y - 1.0) > 1e-6 || std::abs(v.x) > 1e-6) {
            std::printf("FAIL: rotate 90\n");
            fail++;
        }
    }
    // math3d: 透视投影
    {
        Mat4 P = mat4_perspective(deg2rad(60), 4.0/3.0, 1, 100);
        Vec4 np = P * Vec4(0, 0, -1, 1);
        if (std::abs(np.z/np.w + 1.0) > 1e-5) {
            std::printf("FAIL: perspective near plane\n");
            fail++;
        }
    }
    // mesh: 8 顶点立方体，12 面
    {
        Mesh m;
        make_cube(m, 1.0);
        if (m.vertex_count() != 8 || m.face_count() != 12) {
            std::printf("FAIL: cube counts\n");
            fail++;
        }
    }
    // raster: 渲染已知三角形，中心像素非零
    {
        const int W = 32, H = 32;
        uint32_t* pix = new uint32_t[W*H];
        nefu::gfxlib::Buffer buf = { pix, W, H };
        Rasterizer r;
        r.attach(buf);
        uint32_t col = 0xFFFFFFFF;
        r.frag_shader = [](const FragInput&, void* u, uint32_t& out) {
            out = *(uint32_t*)u;
        };
        r.frag_user = &col;
        r.backface_cull = false;
        r.clear(0xFF000000);
        Rasterizer::ClipVert v0,v1,v2;
        v0.clip=Vec4(-0.5,-0.5,0,1); v1.clip=Vec4(0.5,-0.5,0,1); v2.clip=Vec4(0,0.5,0,1);
        r.draw_triangle(v0,v1,v2);
        if ((pix[16*W+16] & 0xFFFFFF) != 0xFFFFFF) {
            std::printf("FAIL: raster center pixel\n");
            fail++;
        }
        r.detach();
        delete[] pix;
    }

    if (fail == 0) std::printf("ALL GFX3D TESTS PASSED\n");
    else           std::printf("FAILURES: %d\n", fail);
    return fail == 0 ? 0 : 1;
}
