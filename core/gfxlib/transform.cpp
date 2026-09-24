// nefuOS graphics library — affine transforms implementation & self test
#include "transform.h"

namespace nefu {
namespace gfxlib {

using nefu::fx::fix;
using nefu::fx::fx_mul;
using nefu::fx::fx_div;
using nefu::fx::fx_cos;
using nefu::fx::fx_sin;
using nefu::fx::FX_ONE;

Mat2x3 mat_identity() {
    Mat2x3 m = {FX_ONE, 0, 0, 0, FX_ONE, 0};
    return m;
}

Mat2x3 mat_translate(fix tx, fix ty) {
    Mat2x3 m = {FX_ONE, 0, tx, 0, FX_ONE, ty};
    return m;
}

Mat2x3 mat_rotate(fix rad) {
    fix c = fx_cos(rad), s = fx_sin(rad);
    Mat2x3 m = {c, -s, 0, s, c, 0};
    return m;
}

Mat2x3 mat_scale(fix sx, fix sy) {
    Mat2x3 m = {sx, 0, 0, 0, sy, 0};
    return m;
}

Mat2x3 mat_shear(fix shx, fix shy) {
    Mat2x3 m = {FX_ONE, shx, 0, shy, FX_ONE, 0};
    return m;
}

Mat2x3 mat_mul(const Mat2x3& a, const Mat2x3& b) {
    Mat2x3 o;
    o.m00 = fx_mul(a.m00, b.m00) + fx_mul(a.m01, b.m10);
    o.m01 = fx_mul(a.m00, b.m01) + fx_mul(a.m01, b.m11);
    o.m02 = fx_mul(a.m00, b.m02) + fx_mul(a.m01, b.m12) + a.m02;
    o.m10 = fx_mul(a.m10, b.m00) + fx_mul(a.m11, b.m10);
    o.m11 = fx_mul(a.m10, b.m01) + fx_mul(a.m11, b.m11);
    o.m12 = fx_mul(a.m10, b.m02) + fx_mul(a.m11, b.m12) + a.m12;
    return o;
}

void mat_transform(const Mat2x3& m, fix x, fix y, fix* ox, fix* oy) {
    *ox = fx_mul(m.m00, x) + fx_mul(m.m01, y) + m.m02;
    *oy = fx_mul(m.m10, x) + fx_mul(m.m11, y) + m.m12;
}

bool mat_inverse(const Mat2x3& m, Mat2x3* out) {
    if (!out) return false;
    fix det = fx_mul(m.m00, m.m11) - fx_mul(m.m01, m.m10);
    if (det == 0) return false;
    out->m00 = fx_div(m.m11, det);
    out->m01 = fx_div(-m.m01, det);
    out->m10 = fx_div(-m.m10, det);
    out->m11 = fx_div(m.m00, det);
    out->m02 = fx_div(fx_mul(m.m01, m.m12) - fx_mul(m.m11, m.m02), det);
    out->m12 = fx_div(fx_mul(m.m10, m.m02) - fx_mul(m.m00, m.m12), det);
    return true;
}

// ---------------------------------------------------------------------
// 3D
// ---------------------------------------------------------------------
Mat3 mat3_rotate_x(fix rad) {
    fix c = fx_cos(rad), s = fx_sin(rad);
    Mat3 m;
    m.m[0][0] = FX_ONE; m.m[0][1] = 0;     m.m[0][2] = 0;
    m.m[1][0] = 0;     m.m[1][1] = c;     m.m[1][2] = -s;
    m.m[2][0] = 0;     m.m[2][1] = s;     m.m[2][2] = c;
    return m;
}

Mat3 mat3_rotate_y(fix rad) {
    fix c = fx_cos(rad), s = fx_sin(rad);
    Mat3 m;
    m.m[0][0] = c;     m.m[0][1] = 0;     m.m[0][2] = s;
    m.m[1][0] = 0;     m.m[1][1] = FX_ONE; m.m[1][2] = 0;
    m.m[2][0] = -s;    m.m[2][1] = 0;     m.m[2][2] = c;
    return m;
}

Mat3 mat3_rotate_z(fix rad) {
    fix c = fx_cos(rad), s = fx_sin(rad);
    Mat3 m;
    m.m[0][0] = c;     m.m[0][1] = -s;    m.m[0][2] = 0;
    m.m[1][0] = s;     m.m[1][1] = c;     m.m[1][2] = 0;
    m.m[2][0] = 0;     m.m[2][1] = 0;     m.m[2][2] = FX_ONE;
    return m;
}

Mat3 mat3_mul(const Mat3& a, const Mat3& b) {
    Mat3 o;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            fix s = 0;
            for (int k = 0; k < 3; k++)
                s += fx_mul(a.m[i][k], b.m[k][j]);
            o.m[i][j] = s;
        }
    return o;
}

Vec3 mat3_apply(const Mat3& m, const Vec3& v) {
    Vec3 o;
    o.x = fx_mul(m.m[0][0], v.x) + fx_mul(m.m[0][1], v.y) + fx_mul(m.m[0][2], v.z);
    o.y = fx_mul(m.m[1][0], v.x) + fx_mul(m.m[1][1], v.y) + fx_mul(m.m[1][2], v.z);
    o.z = fx_mul(m.m[2][0], v.x) + fx_mul(m.m[2][1], v.y) + fx_mul(m.m[2][2], v.z);
    return o;
}

bool project(const Vec3& v, fix fov, int w, int h, int* sx, int* sy) {
    // camera at origin looking down -z; a point at depth fov maps to the
    // projection plane scale 1:1
    fix depth = fx_div(fov, v.z);          // v.z negative in front of camera
    // sx = w/2 + x * fov / (-z) ; use depth = fov / (-z)
    fix negz = -v.z;
    if (negz <= 0) return false;
    fix scale = fx_div(fov, negz);
    fix cx = fx_mul(v.x, scale) + nefu::fx::itofix(w / 2);
    fix cy = -fx_mul(v.y, scale) + nefu::fx::itofix(h / 2);
    *sx = nefu::fx::fixtoi(cx);
    *sy = nefu::fx::fixtoi(cy);
    return true;
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_tr_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_tr_fails++;
    (void)what;
}
bool fx_near(fix a, fix b, fix eps = 64) {   // eps ~ 0.001
    fix d = a > b ? a - b : b - a;
    return d <= eps;
}
} // namespace

int transform_self_test() {
    g_tr_fails = 0;
    using nefu::fx::itofix;
    using nefu::fx::fixtoi;

    // identity leaves points unchanged
    Mat2x3 id = mat_identity();
    fix ox, oy;
    mat_transform(id, itofix(7), itofix(-3), &ox, &oy);
    expect("id-x", ox == itofix(7));
    expect("id-y", oy == itofix(-3));

    // translate by (10, -5)
    Mat2x3 tr = mat_translate(itofix(10), itofix(-5));
    mat_transform(tr, itofix(2), itofix(3), &ox, &oy);
    expect("tr-x", ox == itofix(12));
    expect("tr-y", oy == itofix(-2));

    // scale (3, 2)
    Mat2x3 sc = mat_scale(itofix(3), itofix(2));
    mat_transform(sc, itofix(4), itofix(5), &ox, &oy);
    expect("sc-x", ox == itofix(12));
    expect("sc-y", oy == itofix(10));

    // rotate 90 deg: (1,0) -> (0,1)
    Mat2x3 rot = mat_rotate(nefu::fx::FX_PI_2);
    mat_transform(rot, itofix(1), 0, &ox, &oy);
    expect("rot-x", fx_near(ox, 0, 8));
    expect("rot-y", fx_near(oy, itofix(1), 8));

    // combine: translate then rotate vs mat_mul order
    Mat2x3 comb = mat_mul(mat_rotate(nefu::fx::FX_PI_2), mat_translate(itofix(3), 0));
    // first translate (3,0) then rotate 90 -> (0,3)
    mat_transform(comb, 0, 0, &ox, &oy);
    expect("comb-x", fx_near(ox, 0, 8));
    expect("comb-y", fx_near(oy, itofix(3), 8));

    // inverse: translate+scale then inverse restores
    Mat2x3 fwd = mat_mul(mat_translate(itofix(5), itofix(-2)), mat_scale(itofix(2), itofix(3)));
    Mat2x3 inv;
    expect("inv-ok", mat_inverse(fwd, &inv));
    Mat2x3 back = mat_mul(inv, fwd);
    mat_transform(back, itofix(11), itofix(-7), &ox, &oy);
    expect("inv-restore-x", fx_near(ox, itofix(11), 16));
    expect("inv-restore-y", fx_near(oy, itofix(-7), 16));

    // singular matrix has no inverse
    Mat2x3 sing = mat_scale(0, itofix(1));
    expect("inv-singular", !mat_inverse(sing, &inv));

    // 3D rotations keep length of a unit vector (approx)
    Mat3 rx = mat3_rotate_x(nefu::fx::FX_PI_2);
    Vec3 u = {itofix(1), 0, 0};
    Vec3 ru = mat3_apply(rx, u);
    expect("rot3x-x", fx_near(ru.x, itofix(1), 8) && fx_near(ru.y, 0, 8) && fx_near(ru.z, 0, 8));
    Vec3 uz = {0, 0, itofix(1)};
    Vec3 rz = mat3_apply(rx, uz);
    // rotate_x(PI/2): (0,0,1) -> (0,-1,0)
    expect("rot3x-z->y", fx_near(rz.y, itofix(-1), 8) && fx_near(rz.z, 0, 8));

    // composition: rotate_x then rotate_y
    Mat3 rxy = mat3_mul(mat3_rotate_y(nefu::fx::FX_PI_2), mat3_rotate_x(nefu::fx::FX_PI_2));
    Vec3 v0 = {itofix(1), itofix(2), itofix(3)};
    Vec3 v1 = mat3_apply(rxy, v0);
    (void)v1;

    // projection: point (0,0,-10) with fov 10 lands at screen center
    Vec3 cpt = {0, 0, itofix(-10)};
    int sx, sy;
    expect("proj-center", project(cpt, itofix(10), 800, 600, &sx, &sy));
    expect("proj-center-xy", sx == 400 && sy == 300);
    // point behind camera rejected
    Vec3 behind = {0, 0, itofix(5)};
    expect("proj-behind", !project(behind, itofix(10), 800, 600, &sx, &sy));
    // point to the right of center: (10,0,-10), fov 10 -> offset 10*10/10 = 10
    Vec3 right = {itofix(10), 0, itofix(-10)};
    expect("proj-right", project(right, itofix(10), 800, 600, &sx, &sy));
    expect("proj-right-x", sx >= 409 && sx <= 411 && sy >= 299 && sy <= 301);

    return g_tr_fails;
}

} // namespace gfxlib
} // namespace nefu
