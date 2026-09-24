// ============================================================================
// nefuOS 3D 图形库 —— light: 光照模型
// ----------------------------------------------------------------------------
// 支持：
//   - 光源类型：方向光 / 点光源 / 聚光灯
//   - 光照模型：Phong（反射向量）/ Blinn-Phong（半程向量）
//   - 着色频率：平面着色（flat，逐三角形）/ Gouraud（逐顶点）/ Phong（逐片元）
//   - 衰减：常数 / 线性 / 二次
//   - 雾：线性 / 指数 / 指数平方
//   - 简单投影阴影：把顶点沿光源方向压到地面平面，得到阴影投影矩阵
// ============================================================================
#pragma once
#include "math3d.h"
#include "raster.h"

namespace nefu {
namespace gfx3d {

// 光源类型
enum LightType {
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT,
    LIGHT_SPOT
};

// 单个光源
struct Light {
    LightType type;
    Vec3  position;     // 世界坐标（点/聚光）
    Vec3  direction;     // 世界方向（方向光/聚光），单位化
    Vec3  color;         // RGB [0,1]
    double intensity;
    // 衰减系数：atten = 1/(kc + kl*d + kq*d^2)
    double kc, kl, kq;
    // 聚光：内锥余弦 / 外锥余弦
    double spot_inner, spot_outer;

    Light() : type(LIGHT_DIRECTIONAL), color(1, 1, 1), intensity(1),
              kc(1), kl(0), kq(0), spot_inner(0.9), spot_outer(0.7) {}
};

// 材质（与 mesh::Material 字段对齐，方便互操作）
struct ShadeMaterial {
    Vec3 ambient;
    Vec3 diffuse;
    Vec3 specular;
    double shininess;     // 高光指数
    double alpha;

    ShadeMaterial()
        : ambient(0.1, 0.1, 0.1), diffuse(0.8, 0.8, 0.8),
          specular(0.3, 0.3, 0.3), shininess(32), alpha(1) {}
};

// 雾参数
struct FogParams {
    bool enable;
    int  type;           // 0=linear 1=exp 2=exp2
    Vec3 color;
    double density;      // exp/exp2
    double start, end;   // linear
    FogParams() : enable(false), type(0), color(0.5, 0.5, 0.5),
                  density(0.1), start(1), end(10) {}
};

// 光照环境：光源列表 + 全局环境光 + 雾 + 相机位置（用于视线向量）
struct LightingEnv {
    Light lights[8];
    int   light_count;
    Vec3  global_ambient;
    Vec3  eye_pos;           // 世界坐标相机位置
    bool  use_blinn_phong;   // true=Blinn-Phong，false=Phong
    FogParams fog;

    LightingEnv() : light_count(0), global_ambient(0.1, 0.1, 0.1),
                    use_blinn_phong(true) {}

    void add_light(const Light& l) {
        if (light_count < 8) lights[light_count++] = l;
    }
};

// ----------------------------------------------------------------------------
// 核心光照计算：给定世界位置 p、法线 n（单位）、材质 mat、光照环境 env，
// 返回输出颜色（RGB [0,1]）。
// ----------------------------------------------------------------------------
Vec3 shade_phong(const Vec3& p, const Vec3& n, const Vec3& eye,
                 const ShadeMaterial& mat, const LightingEnv& env);

// 应用雾：根据与相机距离把颜色混入雾色
Vec3 apply_fog(const Vec3& color, double dist, const FogParams& fog);

// 半球环境光：根据法线上下方向混合两种天空/地面颜色
Vec3 hemisphere_light(const Vec3& n, const Vec3& sky, const Vec3& ground);

// 边缘光（rim）：视角与法线夹角越大越亮，用于轮廓辉光
Vec3 rim_light(const Vec3& n, const Vec3& view, const Vec3& rim_color, double power);

// ----------------------------------------------------------------------------
// 投影阴影矩阵：把顶点沿光源方向投影到 y=ground_y 平面上。
// 用法：shadowed_pos = shadow_matrix * world_pos，再用一个暗色画一遍。
// light_dir 指向光源（从地面看），ground_y 地面高度。
// ----------------------------------------------------------------------------
Mat4 shadow_project_matrix(const Vec3& light_dir, double ground_y);

// ----------------------------------------------------------------------------
// 片元着色器（可直接挂到 Rasterizer::frag_shader）。
// user 指向 PhongShaderUser { const LightingEnv* env; const ShadeMaterial* mat; }
// ----------------------------------------------------------------------------
struct PhongShaderUser {
    const LightingEnv* env;
    const ShadeMaterial* mat;
};
void phong_fragment_shader(const FragInput& in, void* user, uint32_t& out_color);

// Gouraud：逐顶点着色。对一个顶点列表预先计算颜色写入 verts[i].color。
// mesh 的 color 字段会被填好，光栅化时自动插值。
void gouraud_shade_vertices(class Mesh& m, const Mat4& model, const LightingEnv& env,
                            const ShadeMaterial& mat);

// self test
int light_self_test();

} // namespace gfx3d
} // namespace nefu
