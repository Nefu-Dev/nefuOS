// ============================================================================
// nefuOS 3D 图形库 —— camera: 相机控制
// ----------------------------------------------------------------------------
// 封装一个可交互相机：
//   - 轨道模式（绕目标旋转，鼠标拖动/方向键）
//   - 第一人称模式（WASD 移动，鼠标看向）
//   - 投影参数（fovy/aspect/znear/zfar）
//   - 生成 view/proj 矩阵
// ============================================================================
#pragma once
#include "math3d.h"

namespace nefu {
namespace gfx3d {

enum CameraMode {
    CAM_ORBIT = 0,
    CAM_FPS = 1,
};

struct Camera {
    CameraMode mode;
    Vec3 eye;          // 世界坐标相机位置
    Vec3 target;       // 轨道模式：看向的目标
    Vec3 up;
    double yaw, pitch; // FPS 模式：欧拉角
    double fovy_deg;
    double aspect;
    double znear, zfar;
    double orbit_dist; // 轨道模式：距离目标距离

    Camera() : mode(CAM_ORBIT), eye(0,0,3), target(0,0,0), up(0,1,0),
               yaw(0), pitch(0), fovy_deg(60), aspect(4.0/3.0),
               znear(0.1), zfar(100), orbit_dist(3) {}

    // 轨道模式旋转
    void orbit(double dx, double dy);
    // 轨道模式缩放
    void zoom(double delta);
    // FPS 模式平移
    void move(double dx, double dy, double dz);

    Mat4 view_matrix() const;
    Mat4 proj_matrix() const;
    Mat4 view_proj() const { return proj_matrix() * view_matrix(); }
};

int camera_self_test();

} // namespace gfx3d
} // namespace nefu
