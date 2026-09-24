// nefuOS graphics library — 2D affine transforms (Q16.16) + 3D wireframe
// projection.  Everything is fixed-point (nefu::fx) so it runs on the bare
// kernel without an FPU.
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"

namespace nefu {
namespace gfxlib {

// 2D affine matrix, row-major, Q16.16:
//   [ m00 m01 m02 ]
//   [ m10 m11 m12 ]
// maps (x,y,1) -> (x',y')
struct Mat2x3 {
    nefu::fx::fix m00, m01, m02;
    nefu::fx::fix m10, m11, m12;
};

Mat2x3 mat_identity();
Mat2x3 mat_translate(nefu::fx::fix tx, nefu::fx::fix ty);
Mat2x3 mat_rotate(nefu::fx::fix rad);                 // counter-clockwise
Mat2x3 mat_scale(nefu::fx::fix sx, nefu::fx::fix sy);
Mat2x3 mat_shear(nefu::fx::fix shx, nefu::fx::fix shy);
Mat2x3 mat_mul(const Mat2x3& a, const Mat2x3& b);     // a applied after b
// transform a point; results written to ox, oy
void mat_transform(const Mat2x3& m, nefu::fx::fix x, nefu::fx::fix y,
                   nefu::fx::fix* ox, nefu::fx::fix* oy);
// inverse; returns false when singular
bool mat_inverse(const Mat2x3& m, Mat2x3* out);

// --- 3D wireframe helpers (right-handed, y-up) ---
struct Vec3 {
    nefu::fx::fix x, y, z;
};
struct Mat3 {                       // 3x3 rotation, Q16.16
    nefu::fx::fix m[3][3];
};
Mat3 mat3_rotate_x(nefu::fx::fix rad);
Mat3 mat3_rotate_y(nefu::fx::fix rad);
Mat3 mat3_rotate_z(nefu::fx::fix rad);
Mat3 mat3_mul(const Mat3& a, const Mat3& b);
Vec3 mat3_apply(const Mat3& m, const Vec3& v);
// perspective projection onto a w x h viewport; camera looks down -z,
// focal distance fov (Q16.16).  Returns false when the point is behind
// the camera.
bool project(const Vec3& v, nefu::fx::fix fov, int w, int h, int* sx, int* sy);

// self test
int transform_self_test();

} // namespace gfxlib
} // namespace nefu
