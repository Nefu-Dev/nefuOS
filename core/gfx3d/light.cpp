// ============================================================================
// nefuOS 3D 图形库 —— light 实现
// ============================================================================
#include "light.h"
#include "mesh.h"

#include <cmath>

namespace nefu {
namespace gfx3d {

static inline double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// ----------------------------------------------------------------------------
// 单个光源的贡献：返回 diffuse+specular 部分（不含环境光）
// ----------------------------------------------------------------------------
static Vec3 light_contribution(const Light& L, const Vec3& p, const Vec3& n,
                               const Vec3& eye, const ShadeMaterial& mat,
                               bool blinn) {
    Vec3 to_light;
    double attenuation = 1.0;

    if (L.type == LIGHT_DIRECTIONAL) {
        to_light = -L.direction.normalized();
    } else {
        Vec3 d = L.position - p;
        double dist = d.length();
        to_light = d / (dist + 1e-9);
        // 衰减
        attenuation = 1.0 / (L.kc + L.kl * dist + L.kq * dist * dist);
        if (L.type == LIGHT_SPOT) {
            // 聚光：根据与主轴夹角裁剪
            Vec3 dir_to_light = (-to_light).normalized();
            double cosang = dir_to_light.dot(L.direction.normalized());
            double spot = clamp01((cosang - L.spot_outer) /
                                  (L.spot_inner - L.spot_outer + 1e-9));
            attenuation *= spot;
        }
    }

    // 漫反射：max(N·L, 0)
    double ndotl = n.dot(to_light);
    double diff = ndotl > 0 ? ndotl : 0;

    // 高光
    double spec = 0;
    if (diff > 0) {
        Vec3 view = (eye - p).normalized();
        if (blinn) {
            Vec3 h = (to_light + view).normalized();
            spec = pow(n.dot(h), mat.shininess);
        } else {
            Vec3 r = reflect(-to_light, n);
            spec = pow(r.dot(view), mat.shininess);
        }
        if (spec < 0) spec = 0;
    }

    Vec3 result(0, 0, 0);
    result += mat.diffuse  * (L.color * (diff * L.intensity * attenuation));
    result += mat.specular * (L.color * (spec * L.intensity * attenuation));
    return result;
}

Vec3 shade_phong(const Vec3& p, const Vec3& n, const Vec3& eye,
                 const ShadeMaterial& mat, const LightingEnv& env) {
    Vec3 N = n.normalized();
    Vec3 color = mat.ambient * env.global_ambient;
    for (int i = 0; i < env.light_count; i++) {
        color += light_contribution(env.lights[i], p, N, eye, mat, env.use_blinn_phong);
    }
    // 雾
    double dist = (p - eye).length();
    color = apply_fog(color, dist, env.fog);
    return Vec3(clamp01(color.x), clamp01(color.y), clamp01(color.z));
}

Vec3 apply_fog(const Vec3& color, double dist, const FogParams& fog) {
    if (!fog.enable) return color;
    double f = 0;
    if (fog.type == 0) {
        f = (fog.end - dist) / (fog.end - fog.start);
    } else if (fog.type == 1) {
        f = exp(-fog.density * dist);
    } else {
        f = exp(-fog.density * fog.density * dist * dist);
    }
    f = clamp01(f);
    return color * f + fog.color * (1.0 - f);
}

Vec3 hemisphere_light(const Vec3& n, const Vec3& sky, const Vec3& ground) {
    // 法线 y 分量 [-1,1] -> [0,1] 插值权重
    double t = (n.y + 1.0) * 0.5;
    return ground * (1.0 - t) + sky * t;
}

Vec3 rim_light(const Vec3& n, const Vec3& view, const Vec3& rim_color, double power) {
    double ndv = n.normalized().dot(view.normalized());
    double rim = 1.0 - clamp01(ndv);
    rim = pow(rim, power);
    return rim_color * rim;
}

// ----------------------------------------------------------------------------
// 投影阴影矩阵（把世界点沿光源方向压到 y = ground_y 平面）
// 推导：光线方程 p + t*dir，要求 y 坐标 == ground_y。
// t = (ground_y - p.y) / dir.y。得到投影矩阵（4x4）。
// ----------------------------------------------------------------------------
Mat4 shadow_project_matrix(const Vec3& light_dir, double ground_y) {
    Vec3 d = light_dir.normalized();
    // 我们希望矩阵 M 满足 M*[p,1]^T = [px + t*dx, ground_y, pz + t*dz, 1]
    // t = (ground_y - py)/dy
    // 即：
    //  x' = px + dx/dy*(ground_y - py)
    //  y' = ground_y
    //  z' = pz + dz/dy*(ground_y - py)
    // 写成齐次矩阵：
    Mat4 M;
    double dx = d.x / d.y, dz = d.z / d.y;
    M.m[0][0] = 1; M.m[0][1] = -dx; M.m[0][2] = 0; M.m[0][3] = dx * ground_y;
    M.m[1][0] = 0; M.m[1][1] = 0;   M.m[1][2] = 0; M.m[1][3] = ground_y;
    M.m[2][0] = 0; M.m[2][1] = -dz; M.m[2][2] = 1; M.m[2][3] = dz * ground_y;
    M.m[3][0] = 0; M.m[3][1] = 0;   M.m[3][2] = 0; M.m[3][3] = 1;
    return M;
}

// ----------------------------------------------------------------------------
// Phong 片元着色器
// ----------------------------------------------------------------------------
void phong_fragment_shader(const FragInput& in, void* user, uint32_t& out_color) {
    PhongShaderUser* u = (PhongShaderUser*)user;
    if (!u || !u->env || !u->mat) { out_color = 0xFFFFFFFF; return; }
    Vec3 c = shade_phong(in.world, in.normal, u->env->eye_pos, *u->mat, *u->env);
    out_color = pack_color(c.x, c.y, c.z, u->mat->alpha);
}

// ----------------------------------------------------------------------------
// Gouraud：逐顶点计算光照，写入顶点 color 字段
// ----------------------------------------------------------------------------
void gouraud_shade_vertices(Mesh& m, const Mat4& model, const LightingEnv& env,
                            const ShadeMaterial& mat) {
    for (int i = 0; i < m.verts.size(); i++) {
        Vec4 wp = model * Vec4(m.verts[i].pos, 1.0);
        // 法线世界变换（忽略非均匀缩放的逆转置修正，教学场景足够）
        Mat3 rot;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) rot.m[r][c] = model.m[r][c];
        Vec3 wn = (rot * m.verts[i].normal).normalized();
        Vec3 c = shade_phong(wp.xyz(), wn, env.eye_pos, mat, env);
        m.verts[i].color = Vec4(c.x, c.y, c.z, mat.alpha);
    }
}

// ============================================================================
// self test
// ============================================================================
int light_self_test() {
    int fail = 0;

    // 方向光沿 +Z 照向原点，法线 +Z 的面应全亮
    {
        LightingEnv env;
        env.use_blinn_phong = true;
        Light l;
        l.type = LIGHT_DIRECTIONAL;
        l.direction = Vec3(0, 0, -1);     // 光沿 -Z 走
        l.color = Vec3(1, 1, 1);
        l.intensity = 1;
        env.add_light(l);
        env.eye_pos = Vec3(0, 0, 5);

        ShadeMaterial mat;
        mat.diffuse = Vec3(1, 1, 1);
        mat.ambient = Vec3(0, 0, 0);
        mat.specular = Vec3(0, 0, 0);

        Vec3 c = shade_phong(Vec3(0, 0, 0), Vec3(0, 0, 1), env.eye_pos, mat, env);
        // N·L = 1，diffuse 全白
        if (c.x < 0.9 || c.x > 1.01) fail++;
    }

    // 背光面应全黑
    {
        LightingEnv env;
        Light l;
        l.direction = Vec3(0, 0, -1);
        env.add_light(l);
        ShadeMaterial mat;
        mat.ambient = Vec3(0, 0, 0);
        Vec3 c = shade_phong(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 0, 5), mat, env);
        if (c.x > 0.01) fail++;
    }

    // 雾：远处颜色应趋近雾色
    {
        FogParams fog;
        fog.enable = true;
        fog.type = 1;
        fog.density = 10.0;   // 极强雾
        fog.color = Vec3(0.5, 0.5, 0.5);
        Vec3 c = apply_fog(Vec3(1, 0, 0), 1.0, fog);
        // 强雾下基本只剩雾色
        if (c.x < 0.4 || c.x > 0.6) fail++;
    }

    // 阴影投影矩阵：点 (0,1,0)，光方向 (0,-1,0)，地面 y=0 -> 投影应为 (0,0,0)
    {
        Mat4 M = shadow_project_matrix(Vec3(0, -1, 0), 0.0);
        Vec4 r = M * Vec4(0, 1, 0, 1);
        if (r.y > 1e-6) fail++;
    }

    // 半球光：朝上法线应接近天空色
    {
        Vec3 c = hemisphere_light(Vec3(0, 1, 0), Vec3(0.5, 0.7, 1.0), Vec3(0.2, 0.1, 0.0));
        if (c.y < 0.6 || c.x > 0.6) fail++;
    }

    // 边缘光：正对视线的面 rim 应接近 0
    {
        Vec3 c = rim_light(Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(1, 1, 1), 4.0);
        if (c.x > 0.01) fail++;
    }

    return fail;
}

} // namespace gfx3d
} // namespace nefu
