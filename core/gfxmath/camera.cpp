// nefuOS gfxmath —— 相机实现 + 自测
#include "gfxmath/camera.h"
#include <cstdio>

namespace nefu {
namespace gfx {

Camera::Camera(const Vec3& p, const Vec3& la, const Vec3& u,
               double fovy_, double aspect_, double zn, double zf)
    : pos(p), look(la), up_ref(u), fovy(fovy_), aspect(aspect_), znear(zn), zfar(zf) {
    rebuild_axes();
}

void Camera::rebuild_axes() {
    fwd = (look - pos).normalized();
    rgt = fwd.cross(up_ref).normalized();
    upv = rgt.cross(fwd).normalized();
}

Mat4 Camera::view_matrix() const {
    // 把相机坐标变换到原点：平移 + 旋转（相机基向量为行）
    Mat4 r;
    r.m[0][0] = rgt.x;  r.m[0][1] = rgt.y;  r.m[0][2] = rgt.z;
    r.m[1][0] = upv.x;  r.m[1][1] = upv.y;  r.m[1][2] = upv.z;
    r.m[2][0] = -fwd.x; r.m[2][1] = -fwd.y; r.m[2][2] = -fwd.z;
    r.m[3][3] = 1;
    Mat4 t = Mat4::translation(-pos.x, -pos.y, -pos.z);
    return r * t;
}

void Camera::move(double dx, double dy, double dz) {
    pos += rgt * dx + upv * dy + fwd * dz;
    look += rgt * dx + upv * dy + fwd * dz;
}

void Camera::pitch(double angle) {
    // 绕 rgt 轴旋转 fwd 和 upv
    double c = std::cos(angle), s = std::sin(angle);
    Vec3 nf = fwd * c + upv * s;
    Vec3 nu = -fwd * s + upv * c;
    fwd = nf.normalized();
    upv = nu.normalized();
    look = pos + fwd;
}

void Camera::yaw(double angle) {
    double c = std::cos(angle), s = std::sin(angle);
    Vec3 nf = fwd * c - rgt * s;
    rgt = fwd * s + rgt * c;
    fwd = nf.normalized();
    rgt = rgt.normalized();
    upv = rgt.cross(fwd).normalized();
    look = pos + fwd;
}

void Camera::set_perspective(double fovy_, double aspect_, double zn, double zf) {
    fovy = fovy_; aspect = aspect_; znear = zn; zfar = zf;
}

// ---- self test ----
int Camera::self_test() {
    int fails = 0;
    // 1. 基向量正交性
    {
        Camera c(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 1.0, 1.5, 0.1, 100);
        if (std::abs(c.forward().dot(c.right())) > 1e-9) fails++;
        if (std::abs(c.forward().dot(c.up())) > 1e-9) fails++;
        if (std::abs(c.right().dot(c.up())) > 1e-9) fails++;
        // 沿 -Z 看：forward 应为 (0,0,-1)
        if (std::abs(c.forward().z + 1) > 1e-9) fails++;
        if (std::abs(c.forward().x) > 1e-9 || std::abs(c.forward().y) > 1e-9) fails++;
    }
    // 2. 视图矩阵：把注视点变换到 -Z 轴上
    {
        Camera c(Vec3(10, 0, 0), Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0, 1.5, 0.1, 100);
        Mat4 V = c.view_matrix();
        Vec3 origin_view = V.transform(Vec3(0, 0, 0));
        if (std::abs(origin_view.z + 10) > 1e-9) fails++;   // 原点在相机前方 -10
        Vec3 cam_pos_view = V.transform(c.position());
        if (cam_pos_view.x != 0 || cam_pos_view.y != 0 || cam_pos_view.z != 0) fails++;
    }
    // 3. 组合矩阵可逆性：view_proj 非奇异
    {
        Camera c(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0), 1.0, 1.5, 0.1, 100);
        Mat4 VP = c.view_proj_matrix();
        Mat4 I = VP * VP.inverse();
        bool id = true;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (std::abs(I.m[i][j] - (i == j ? 1.0 : 0.0)) > 1e-6) id = false;
        if (!id) fails++;
    }
    // 4. 移动与俯仰偏航
    {
        Camera c(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 1.0, 1.5, 0.1, 100);
        c.move(0, 0, 5);   // 前进 5
        if (std::abs(c.position().z + 5) > 1e-9) fails++;
        c.yaw(3.14159265358979 / 2);   // 左转 90 度：现在看 -X
        Vec3 f = c.forward();
        if (std::abs(f.x + 1) > 1e-9 || std::abs(f.z) > 1e-6) fails++;
    }
    // 5. 视线方向一致性
    {
        Camera c(Vec3(1, 2, 3), Vec3(4, 2, 3), Vec3(0, 1, 0), 1.0, 1.0, 0.1, 10);
        Vec3 d = c.forward();
        if (std::abs(d.x - 1) > 1e-9 || std::abs(d.y) > 1e-9 || std::abs(d.z) > 1e-9) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
