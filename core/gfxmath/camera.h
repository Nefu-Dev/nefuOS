// nefuOS gfxmath —— 相机 camera
// 教学版：透视相机（位置、朝向、视锥参数）与视图矩阵、投影矩阵。
// 用于把 3D 场景变换到 2D 屏幕。class / STL / cmath / 中文注释。
#pragma once
#include <cmath>
#include "gfxmath/vec3.h"
#include "gfxmath/mat4.h"

namespace nefu {
namespace gfx {

// 透视相机
class Camera {
public:
    // 构造：位置、注视点、向上方向、垂直视角（弧度）、宽高比、近远面
    Camera(const Vec3& pos, const Vec3& look_at, const Vec3& up,
           double fovy, double aspect, double znear, double zfar);

    // 视图矩阵（世界 -> 相机空间）
    Mat4 view_matrix() const;
    // 投影矩阵
    Mat4 proj_matrix() const { return Mat4::perspective(fovy, aspect, znear, zfar); }
    // 视图+投影组合矩阵
    Mat4 view_proj_matrix() const { return proj_matrix() * view_matrix(); }

    // 相机基向量（单位）
    Vec3 forward() const { return fwd; }
    Vec3 right() const { return rgt; }
    Vec3 up() const { return upv; }
    Vec3 position() const { return pos; }

    // 移动相机（沿自身基向量）
    void move(double dx, double dy, double dz);   // 沿 right/up/forward
    // 绕自身 right 轴俯仰（pitch，弧度）
    void pitch(double angle);
    // 绕自身 up 轴偏航（yaw，弧度）
    void yaw(double angle);

    // 更新视锥参数
    void set_perspective(double fovy_, double aspect_, double znear_, double zfar_);

    // ---- self test ----
    static int self_test();

private:
    void rebuild_axes();   // 由 pos/look 重建 fwd/rgt/upv

    Vec3 pos, look, up_ref;
    double fovy, aspect, znear, zfar;
    Vec3 fwd, rgt, upv;
};

} // namespace gfx
} // namespace nefu
