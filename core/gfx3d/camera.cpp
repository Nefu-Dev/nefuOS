// ============================================================================
// nefuOS 3D 图形库 —— camera 实现
// ============================================================================
#include "camera.h"
#include <cmath>

namespace nefu {
namespace gfx3d {

void Camera::orbit(double dx, double dy) {
    yaw += dx;
    pitch += dy;
    if (pitch > 1.5) pitch = 1.5;
    if (pitch < -1.5) pitch = -1.5;
    eye.x = target.x + orbit_dist * cos(pitch) * sin(yaw);
    eye.y = target.y + orbit_dist * sin(pitch);
    eye.z = target.z + orbit_dist * cos(pitch) * cos(yaw);
}

void Camera::zoom(double delta) {
    orbit_dist += delta;
    if (orbit_dist < 0.1) orbit_dist = 0.1;
    if (orbit_dist > 50) orbit_dist = 50;
    orbit(0, 0);
}

void Camera::move(double dx, double dy, double dz) {
    Vec3 forward = (target - eye).normalized();
    Vec3 right = forward.cross(up).normalized();
    eye = eye + right * dx + up * dy + forward * dz;
    target = eye + forward;
}

Mat4 Camera::view_matrix() const {
    return mat4_look_at(eye, target, up);
}

Mat4 Camera::proj_matrix() const {
    return mat4_perspective(deg2rad(fovy_deg), aspect, znear, zfar);
}

int camera_self_test() {
    int fail = 0;
    Camera c;
    c.orbit(0.5, 0.3);
    Mat4 v = c.view_matrix();
    // 视图矩阵不应是单位矩阵
    if (v.m[3][2] == 0) fail++;
    c.zoom(1.0);
    if (c.orbit_dist < 3.9) fail++;
    return fail;
}

} // namespace gfx3d
} // namespace nefu
