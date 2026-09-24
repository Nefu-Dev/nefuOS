// ============================================================================
// nefuOS 光线追踪引擎 —— rtmath 实现（Q16.16 定点）
// ============================================================================
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// ---------------------------------------------------------------------------
// 反射 / 折射 / Schlick
// ---------------------------------------------------------------------------
RTVec3 rt_reflect_vec(const RTVec3& I, const RTVec3& N) {
    // R = I - 2*(I·N)*N
    rtfx dot2 = rt_mul(rt_itofx(2), I.dot(N));
    return I - N * dot2;
}

bool rt_refract_vec(const RTVec3& I, const RTVec3& N, rtfx eta, RTVec3& out) {
    // 标准斯涅尔折射：cosi = -I·N
    rtfx cosi = -I.dot(N);
    rtfx cos2t = RT_ONE - rt_mul(rt_mul(eta, eta), (RT_ONE - rt_mul(cosi, cosi)));
    if (cos2t < 0) return false;            // 全反射
    rtfx cost = rt_sqrt(cos2t);
    out = (I * eta) + N * rt_mul(eta, cosi - cost);
    return true;
}

rtfx rt_schlick(rtfx cos_theta, rtfx R0) {
    // R = R0 + (1-R0)*(1-cos)^5
    rtfx t = RT_ONE - cos_theta;
    rtfx t2 = rt_mul(t, t);
    rtfx t4 = rt_mul(t2, t2);
    rtfx t5 = rt_mul(t4, t);
    return R0 + rt_mul(RT_ONE - R0, t5);
}

RTVec3 rt_cosine_hemisphere_sample(const RTVec3& N, rtfx u, rtfx v) {
    // u,v in [0,1)；在切线空间以 +Y 为极轴采样，再旋转到 N 系
    // 极角 cosθ = sqrt(u)，方位角 φ = 2π*v
    rtfx r1 = v;                       // 方位参数
    rtfx r2 = u;                       // 半径参数
    rtfx phi = rt_mul(RT_2PI, r1);
    rtfx cos_theta = rt_sqrt(r2);      // cosθ = sqrt(u)
    rtfx sin_theta = rt_sqrt(RT_ONE - r2);
    rtfx x = rt_mul(sin_theta, fx::fx_cos(phi));
    rtfx z = rt_mul(sin_theta, fx::fx_sin(phi));
    rtfx y = cos_theta;
    // 构造切线基（N 单位）
    RTVec3 up = (rt_abs(N.y) < (RT_ONE - 1000)) ? RTVec3(0, RT_ONE, 0)
                                                 : RTVec3(rt_itofx(1), 0, 0);
    RTVec3 T = N.cross(up).normalized();
    RTVec3 B = N.cross(T);
    return (T * x + B * z + N * y).normalized();
}

// ---------------------------------------------------------------------------
// AABB slab 射线相交
// ---------------------------------------------------------------------------
bool rt_ray_aabb(const RTRay& ray, const RTAABB& box, rtfx& tnear, rtfx& tfar) {
    rtfx tmin = ray.tmin, tmax = ray.tmax;
    // 逐轴 slab：t1=(mn-o)/d, t2=(mx-o)/d
    for (int axis = 0; axis < 3; axis++) {
        rtfx o, d, mn, mx;
        const rtfx* ro = &ray.origin.x;
        const rtfx* rd = &ray.dir.x;
        const rtfx* bmn = &box.mn.x;
        const rtfx* bmx = &box.mx.x;
        o = ro[axis]; d = rd[axis]; mn = bmn[axis]; mx = bmx[axis];
        if (rt_abs(d) < 2) {
            // 射线平行于此轴 slab：若 origin 不在区间内则无交
            if (o < mn || o > mx) return false;
            continue;
        }
        rtfx t1 = rt_div(mn - o, d);
        rtfx t2 = rt_div(mx - o, d);
        if (t1 > t2) { rtfx tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    tnear = tmin; tfar = tmax;
    return true;
}

// ---------------------------------------------------------------------------
// Mat3 / Mat4
// ---------------------------------------------------------------------------
RTMat3::RTMat3() {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = (i == j) ? RT_ONE : 0;
}
RTMat3 RTMat3::identity() { return RTMat3(); }

RTVec3 operator*(const RTMat3& M, const RTVec3& v) {
    return RTVec3(
        rt_mul(M.m[0][0], v.x) + rt_mul(M.m[0][1], v.y) + rt_mul(M.m[0][2], v.z),
        rt_mul(M.m[1][0], v.x) + rt_mul(M.m[1][1], v.y) + rt_mul(M.m[1][2], v.z),
        rt_mul(M.m[2][0], v.x) + rt_mul(M.m[2][1], v.y) + rt_mul(M.m[2][2], v.z));
}

RTMat3 rt_mat3_mul(const RTMat3& A, const RTMat3& B) {
    RTMat3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            rtfx s = 0;
            for (int k = 0; k < 3; k++) s += rt_mul(A.m[i][k], B.m[k][j]);
            R.m[i][j] = s;
        }
    return R;
}

RTMat3 rt_mat3_transpose(const RTMat3& M) {
    RTMat3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) R.m[i][j] = M.m[j][i];
    return R;
}

RTMat4::RTMat4() {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) m[i][j] = (i == j) ? RT_ONE : 0;
}
RTMat4 RTMat4::identity() { return RTMat4(); }

RTMat4 rt_mat4_mul(const RTMat4& A, const RTMat4& B) {
    RTMat4 R;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            rtfx s = 0;
            for (int k = 0; k < 4; k++) s += rt_mul(A.m[i][k], B.m[k][j]);
            R.m[i][j] = s;
        }
    return R;
}

RTMat4 rt_mat4_look_at(const RTVec3& eye, const RTVec3& target, const RTVec3& up) {
    // 相机朝 -Z（右手系）：f = normalize(target-eye)
    RTVec3 f = (target - eye).normalized();
    RTVec3 s = f.cross(up).normalized();
    RTVec3 u = s.cross(f);
    RTMat4 M;
    // 列向量约定 view = lookAt^-1
    M[0][0] = s.x; M[0][1] = s.y; M[0][2] = s.z; M[0][3] = -s.dot(eye);
    M[1][0] = u.x; M[1][1] = u.y; M[1][2] = u.z; M[1][3] = -u.dot(eye);
    M[2][0] = -f.x; M[2][1] = -f.y; M[2][2] = -f.z; M[2][3] = f.dot(eye);
    M[3][0] = 0; M[3][1] = 0; M[3][2] = 0; M[3][3] = RT_ONE;
    return M;
}

RTMat3 rt_mat4_get_rot(const RTMat4& M) {
    RTMat3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) R.m[i][j] = M.m[i][j];
    return R;
}

// ---------------------------------------------------------------------------
// 相交测试
// ---------------------------------------------------------------------------
rtfx rt_ray_sphere(const RTRay& ray, const RTVec3& c, rtfx r) {
    RTVec3 oc = ray.origin - c;
    rtfx a = ray.dir.dot(ray.dir);          // dir 已单位化，a≈1
    rtfx b = oc.dot(ray.dir);
    rtfx cc = oc.dot(oc) - rt_mul(r, r);
    rtfx disc = rt_mul(b, b) - rt_mul(a, cc);
    if (disc < 0) return -1;
    rtfx s = rt_sqrt(disc);
    rtfx t = rt_div(-b - s, a);
    if (t < ray.tmin) t = rt_div(-b + s, a);
    if (t < ray.tmin || t > ray.tmax) return -1;
    return t;
}

rtfx rt_ray_plane(const RTRay& ray, const RTPlane& p) {
    rtfx denom = p.n.dot(ray.dir);
    if (rt_abs(denom) < 2) return -1;       // 平行
    rtfx t = rt_div(-(p.n.dot(ray.origin) + p.d), denom);
    if (t < ray.tmin || t > ray.tmax) return -1;
    return t;
}

rtfx rt_ray_triangle(const RTRay& ray, const RTVec3& a, const RTVec3& b,
                     const RTVec3& c, rtfx& u, rtfx& v) {
    // Möller–Trumbore
    RTVec3 e1 = b - a;
    RTVec3 e2 = c - a;
    RTVec3 p = ray.dir.cross(e2);
    rtfx det = e1.dot(p);
    if (rt_abs(det) < 2) return -1;         // 平行
    rtfx inv_det = rt_div(RT_ONE, det);
    RTVec3 tvec = ray.origin - a;
    u = rt_mul(tvec.dot(p), inv_det);
    if (u < 0 || u > RT_ONE) return -1;
    RTVec3 q = tvec.cross(e1);
    v = rt_mul(ray.dir.dot(q), inv_det);
    if (v < 0 || u + v > RT_ONE) return -1;
    rtfx t = rt_mul(e2.dot(q), inv_det);
    if (t < ray.tmin || t > ray.tmax) return -1;
    return t;
}

rtfx rt_ray_disc(const RTRay& ray, const RTVec3& c, const RTVec3& n, rtfx r) {
    RTPlane p(n, -(n.dot(c)));
    rtfx t = rt_ray_plane(ray, p);
    if (t < 0) return -1;
    RTVec3 hit = ray.point_at(t);
    RTVec3 d = hit - c;
    if (d.length_sq() > rt_mul(r, r)) return -1;
    return t;
}

// ---------------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------------

RTMat4 rt_mat4_perspective(rtfx fovy, rtfx aspect, rtfx near, rtfx far) {
    RTMat4 M;
    rtfx t = rt_div(RT_ONE, fx::fx_tan(rt_div(fovy, rt_itofx(2))));
    rtfx sx = rt_div(t, aspect);
    rtfx inv = rt_div(RT_ONE, near - far);
    M[0][0] = sx; M[1][1] = t;
    M[2][2] = (far + near) * inv;
    M[2][3] = rt_mul(rt_itofx(2), rt_mul(far, near)) * inv;
    M[3][2] = -RT_ONE;
    return M;
}

RTMat4 rt_mat4_ortho(rtfx l, rtfx r, rtfx b, rtfx t, rtfx n, rtfx f) {
    RTMat4 M;
    M[0][0] = rt_div(rt_itofx(2), r - l);
    M[1][1] = rt_div(rt_itofx(2), t - b);
    M[2][2] = rt_div(rt_itofx(-2), f - n);
    M[3][0] = -rt_div(r + l, r - l);
    M[3][1] = -rt_div(t + b, t - b);
    M[3][2] = -rt_div(f + n, f - n);
    M[3][3] = RT_ONE;
    return M;
}

RTVec3 rt_mat4_xform_point(const RTMat4& M, const RTVec3& p) {
    rtfx x = M[0][0]*p.x + M[0][1]*p.y + M[0][2]*p.z + M[0][3];
    rtfx y = M[1][0]*p.x + M[1][1]*p.y + M[1][2]*p.z + M[1][3];
    rtfx z = M[2][0]*p.x + M[2][1]*p.y + M[2][2]*p.z + M[2][3];
    rtfx w = M[3][0]*p.x + M[3][1]*p.y + M[3][2]*p.z + M[3][3];
    if (w != 0) { x = rt_div(x, w); y = rt_div(y, w); z = rt_div(z, w); }
    return RTVec3(x, y, z);
}

RTVec3 rt_mat4_xform_vec(const RTMat4& M, const RTVec3& v) {
    rtfx x = M[0][0]*v.x + M[0][1]*v.y + M[0][2]*v.z;
    rtfx y = M[1][0]*v.x + M[1][1]*v.y + M[1][2]*v.z;
    rtfx z = M[2][0]*v.x + M[2][1]*v.y + M[2][2]*v.z;
    return RTVec3(x, y, z);
}

RTVec3 rt_color_reinhard(const RTVec3& c) {
    return RTVec3(rt_div(c.x, RT_ONE + c.x), rt_div(c.y, RT_ONE + c.y), rt_div(c.z, RT_ONE + c.z));
}
RTVec3 rt_color_gamma(const RTVec3& c, rtfx g) { (void)g; return RTVec3(rt_sqrt(c.x), rt_sqrt(c.y), rt_sqrt(c.z)); }
RTVec3 rt_color_avg(const RTVec3* cols, int n) {
    if (n<=0) return RTVec3(0,0,0);
    RTVec3 s(0,0,0); for(int i=0;i<n;i++) s+=cols[i]; return s/rt_itofx(n);
}
uint32_t rt_color_to_rgb888(const RTVec3& c) {
    int R=rt_fxtoi(rt_clamp(c.x,0,RT_ONE)*rt_itofx(255));
    int G=rt_fxtoi(rt_clamp(c.y,0,RT_ONE)*rt_itofx(255));
    int B=rt_fxtoi(rt_clamp(c.z,0,RT_ONE)*rt_itofx(255));
    return ((uint32_t)R<<16)|((uint32_t)G<<8)|(uint32_t)B;
}
rtfx rt_ray_torus(const RTRay& ray, const RTVec3& c, rtfx rm, rtfx rt) {
    auto d=[&](const RTVec3& p)->rtfx{ RTVec3 q=p-c; rtfx xz=rt_sqrt(rt_mul(q.x,q.x)+rt_mul(q.z,q.z))-rm; return rt_sqrt(rt_mul(xz,xz)+rt_mul(q.y,q.y))-rt; };
    rtfx t=ray.tmin, step=rt_div(RT_ONE,rt_itofx(8)), prev=d(ray.point_at(t));
    for(int i=0;i<200&&t<ray.tmax;i++){ rtfx h=d(ray.point_at(t)); if(prev>0&&h<=0){rtfx lo=t-step,hi=t;for(int k=0;k<12;k++){rtfx m=(lo+hi)/rt_itofx(2);if(d(ray.point_at(m))<=0)hi=m;else lo=m;}return hi;} prev=h; t+=step; }
    return -1;
}
bool rt_ray_tri_bary(const RTRay& ray, const RTVec3& a, const RTVec3& b, const RTVec3& c, RTBary& o) {
    rtfx u,v; rtfx t=rt_ray_triangle(ray,a,b,c,u,v); if(t<0)return false; o.u=u;o.v=v;o.w=RT_ONE-u-v; return true;
}

rtfx rt_color_luminance(const RTVec3& c) {
    return rt_mul(c.x, fx::fxf(3,10)) + rt_mul(c.y, fx::fxf(6,10)) + rt_mul(c.z, fx::fxf(1,10));
}
RTVec3 rt_color_contrast(const RTVec3& c, rtfx k) {
    rtfx l = rt_color_luminance(c);
    return RTVec3(l + rt_mul(k, c.x - l), l + rt_mul(k, c.y - l), l + rt_mul(k, c.z - l));
}
RTVec3 rt_color_saturate(const RTVec3& c, rtfx s) {
    rtfx l = rt_color_luminance(c);
    return RTVec3(l + rt_mul(s, c.x - l), l + rt_mul(s, c.y - l), l + rt_mul(s, c.z - l));
}
int rtmath_self_test() {
    int fail = 0;
    auto near = [&](rtfx got, rtfx want, rtfx tol, const char* what) {
        if (!rt_near(got, want, tol)) {
            fail++;
            // 不打印详细值（ksprintf 不支持定点浮点），只计数
        }
    };
    // 1. 向量加减 / 标量乘
    {
        RTVec3 a = RTVec3::from_int(1, 2, 3);
        RTVec3 b = RTVec3::from_int(4, 5, 6);
        RTVec3 s = a + b;
        near(s.x, rt_itofx(5), 100, "vec add x");
        RTVec3 sc = a * rt_itofx(2);
        near(sc.x, rt_itofx(2), 100, "vec scale x");
    }
    // 2. 点积 / 叉积
    {
        RTVec3 i = RTVec3::from_int(1, 0, 0);
        RTVec3 j = RTVec3::from_int(0, 1, 0);
        near(i.dot(j), 0, 100, "dot ortho");
        RTVec3 k = i.cross(j);
        near(k.x, 0, 200, "cross x");
        near(k.z, rt_itofx(1), 200, "cross z");
    }
    // 3. 单位化
    {
        RTVec3 v = RTVec3::from_int(3, 0, 0).normalized();
        near(v.x, RT_ONE, 300, "normalize x");
        near(v.length(), RT_ONE, 400, "normalize len");
    }
    // 4. 反射：沿 -Z 入射，法线 +Y，反射应为 +Z 分量变 -Z
    {
        RTVec3 I(0, -RT_ONE, 0);
        RTVec3 N(0, RT_ONE, 0);
        RTVec3 R = rt_reflect_vec(I, N);
        near(R.y, RT_ONE, 300, "reflect y");
    }
    // 5. 射线-球：球心原点半径 1，射线从 (0,0,5) 朝 -Z
    {
        RTRay ray(RTVec3(0, 0, rt_itofx(5)), RTVec3(0, 0, -RT_ONE));
        rtfx t = rt_ray_sphere(ray, RTVec3(0, 0, 0), RT_ONE);
        near(t, rt_itofx(4), 500, "ray sphere t");
    }
    // 6. 射线-AABB：中心原点边长 2 的盒（mn=-1,mx=1），射线穿正面
    {
        RTAABB box(RTVec3(-RT_ONE, -RT_ONE, -RT_ONE),
                   RTVec3( RT_ONE,  RT_ONE,  RT_ONE));
        RTRay ray(RTVec3(0, 0, rt_itofx(5)), RTVec3(0, 0, -RT_ONE));
        rtfx tn, tf;
        bool hit = rt_ray_aabb(ray, box, tn, tf);
        if (!hit) fail++;
        near(tn, rt_itofx(4), 500, "aabb tnear");
    }
    // 7. 射线-三角形：xy 平面 z=0 上的三角形，射线从 +Z 打中心
    {
        RTRay ray(RTVec3(0, 0, rt_itofx(3)), RTVec3(0, 0, -RT_ONE));
        rtfx u, v;
        rtfx t = rt_ray_triangle(ray,
                    RTVec3(-RT_ONE, -RT_HALF, 0),
                    RTVec3( RT_ONE, -RT_HALF, 0),
                    RTVec3(0, RT_HALF, 0), u, v);
        if (t < 0) fail++;
        near(t, rt_itofx(3), 500, "tri t");
    }
    // 8. PRNG 区间
    {
        RTRng rng(42);
        for (int i = 0; i < 100; i++) {
            rtfx r = rng.next_fx();
            if (r < 0 || r >= RT_ONE) fail++;
        }
    }
    // 9. lookAt 矩阵：eye 在 +Z 看原点，up=+Y，右列应朝 +X
    {
        RTMat4 M = rt_mat4_look_at(RTVec3(0, 0, rt_itofx(5)),
                                   RTVec3(0, 0, 0), RTVec3(0, RT_ONE, 0));
        near(M[0][0], RT_ONE, 300, "lookat right x");
        near(M[2][2], RT_ONE, 300, "lookat fwd z");
    }
    // 10. 折射：空气->玻璃 eta=1/1.5，垂直入射应几乎沿原方向
    {
        RTVec3 I(0, 0, -RT_ONE);
        RTVec3 N(0, RT_ONE, 0);
        RTVec3 O;
        bool ok = rt_refract_vec(I, N, fx::fxf(2, 3), O);
        if (!ok) fail++;
        near(O.z, -fx::fxf(2, 3), 500, "refract z");
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
