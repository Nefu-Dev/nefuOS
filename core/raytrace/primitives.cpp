// ============================================================================
// nefuOS 光线追踪引擎 —— primitives 实现（Q16.16 定点）
// ============================================================================
#include "primitives.h"

namespace nefu {
namespace raytrace {

// ---------------------------------------------------------------------------
// 构造便捷函数
// ---------------------------------------------------------------------------
Primitive Primitive::make_sphere(const RTVec3& c, rtfx r, int mat) {
    Primitive p; p.type = PRIM_SPHERE; p.center = c; p.radius = r; p.material = mat;
    return p;
}
Primitive Primitive::make_plane(const RTVec3& n, rtfx d, int mat) {
    Primitive p; p.type = PRIM_PLANE; p.normal = n.normalized(); p.radius = 0;
    p.d = d; p.material = mat;
    return p;
}
Primitive Primitive::make_triangle(const RTVec3& a, const RTVec3& b,
                                   const RTVec3& c, int mat) {
    Primitive p; p.type = PRIM_TRIANGLE; p.pa = a; p.pb = b; p.pc = c; p.material = mat;
    return p;
}
Primitive Primitive::make_box(const RTVec3& mn, const RTVec3& mx, int mat) {
    Primitive p; p.type = PRIM_BOX; p.bmin = mn; p.bmax = mx; p.material = mat;
    return p;
}
Primitive Primitive::make_torus(const RTVec3& c, rtfx R, rtfx r, int mat) {
    Primitive p; p.type = PRIM_TORUS; p.center = c; p.radius = R; p.radius2 = r;
    p.axis = RTVec3(0, RT_ONE, 0); p.material = mat;
    return p;
}
Primitive Primitive::make_cylinder(const RTVec3& c, rtfx r, rtfx h, int mat) {
    Primitive p; p.type = PRIM_CYLINDER; p.center = c; p.radius = r; p.radius2 = h;
    p.axis = RTVec3(0, RT_ONE, 0); p.material = mat;
    return p;
}
Primitive Primitive::make_cone(const RTVec3& c, rtfx r, rtfx h, int mat) {
    Primitive p; p.type = PRIM_CONE; p.center = c; p.radius = r; p.radius2 = h;
    p.axis = RTVec3(0, RT_ONE, 0); p.material = mat;
    return p;
}
Primitive Primitive::make_disc(const RTVec3& c, const RTVec3& n, rtfx r, int mat) {
    Primitive p; p.type = PRIM_DISC; p.center = c; p.normal = n.normalized();
    p.radius = r; p.material = mat;
    return p;
}

// ---------------------------------------------------------------------------
// 包围盒
// ---------------------------------------------------------------------------
RTAABB Primitive::bounds() const {
    RTAABB b;
    switch (type) {
    case PRIM_SPHERE: {
        RTVec3 e(radius, radius, radius);
        b.expand(center - e); b.expand(center + e);
        break;
    }
    case PRIM_PLANE: {
        // 无限平面：给一个巨大的盒（±100）
        RTVec3 big = RTVec3(rt_itofx(100), rt_itofx(100), rt_itofx(100));
        b.expand(center - big); b.expand(center + big);
        // 平面经过点 center - normal*d
        break;
    }
    case PRIM_TRIANGLE:
        b.expand(pa); b.expand(pb); b.expand(pc);
        break;
    case PRIM_BOX:
        b = RTAABB(bmin, bmax);
        break;
    case PRIM_TORUS: {
        rtfx R = radius, r = radius2;
        rtfx ext = R + r;
        RTVec3 e(ext, r, ext);
        b.expand(center - e); b.expand(center + e);
        break;
    }
    case PRIM_CYLINDER: {
        rtfx halfh = rt_mul(radius2, RT_HALF);
        RTVec3 e(radius, halfh, radius);
        b.expand(center - e); b.expand(center + e);
        break;
    }
    case PRIM_CONE: {
        rtfx halfh = rt_mul(radius2, RT_HALF);
        RTVec3 e(radius, halfh, radius);
        b.expand(center - e); b.expand(center + e);
        break;
    }
    case PRIM_DISC: {
        RTVec3 e(radius, radius, radius);
        b.expand(center - e); b.expand(center + e);
        break;
    }
    default: break;
    }
    return b;
}

// ---------------------------------------------------------------------------
// 各图元求交
// ---------------------------------------------------------------------------
// 圆环隐式函数（局部空间，轴 +Y）：f = (sqrt(x^2+z^2)-R)^2 + y^2 - r^2
static rtfx torus_implicit(const RTVec3& p, rtfx R, rtfx r) {
    rtfx xz2 = rt_mul(p.x, p.x) + rt_mul(p.z, p.z);
    rtfx rho = rt_sqrt(xz2);
    rtfx dr = rho - R;
    return rt_mul(dr, dr) + rt_mul(p.y, p.y) - rt_mul(r, r);
}

bool Primitive::intersect(const RTRay& ray, rtfx& t, RTVec3& nrm,
                          RTVec3& pt, RTVec2& uv) const {
    t = -1; nrm = RTVec3(0, 1, 0); pt = RTVec3(); uv = RTVec2(0, 0);
    switch (type) {
    case PRIM_SPHERE: {
        rtfx tt = rt_ray_sphere(ray, center, radius);
        if (tt < 0) return false;
        t = tt;
        pt = ray.point_at(t);
        nrm = (pt - center).normalized();
        break;
    }
    case PRIM_PLANE: {
        RTPlane pl(normal, d);
        rtfx tt = rt_ray_plane(ray, pl);
        if (tt < 0) return false;
        t = tt;
        pt = ray.point_at(t);
        nrm = normal;
        // 背面检测：射线从背面来则翻转法线
        if (nrm.dot(ray.dir) > 0) nrm = -nrm;
        break;
    }
    case PRIM_TRIANGLE: {
        rtfx u, v;
        rtfx tt = rt_ray_triangle(ray, pa, pb, pc, u, v);
        if (tt < 0) return false;
        t = tt;
        pt = ray.point_at(t);
        RTVec3 e1 = pb - pa, e2 = pc - pa;
        nrm = e1.cross(e2).normalized();
        if (nrm.dot(ray.dir) > 0) nrm = -nrm;
        uv = RTVec2(u, v);
        break;
    }
    case PRIM_BOX: {
        RTAABB box(bmin, bmax);
        rtfx tn, tf;
        if (!rt_ray_aabb(ray, box, tn, tf)) return false;
        rtfx hit = (tn < ray.tmin) ? tf : tn;
        if (hit < ray.tmin || hit > ray.tmax) return false;
        t = hit;
        pt = ray.point_at(t);
        // 法线：看哪个面最贴近
        RTVec3 c = box.center();
        RTVec3 e = (bmax - bmin);
        rtfx dx = rt_abs(pt.x - c.x);
        rtfx dy = rt_abs(pt.y - c.y);
        rtfx dz = rt_abs(pt.z - c.z);
        rtfx hx = rt_mul(e.x, RT_HALF), hy = rt_mul(e.y, RT_HALF), hz = rt_mul(e.z, RT_HALF);
        if (dx >= hy && dx >= hz) nrm = RTVec3(pt.x > c.x ? RT_ONE : -RT_ONE, 0, 0);
        else if (dy >= hz) nrm = RTVec3(0, pt.y > c.y ? RT_ONE : -RT_ONE, 0);
        else nrm = RTVec3(0, 0, pt.z > c.z ? RT_ONE : -RT_ONE);
        break;
    }
    case PRIM_DISC: {
        rtfx tt = rt_ray_disc(ray, center, normal, radius);
        if (tt < 0) return false;
        t = tt;
        pt = ray.point_at(t);
        nrm = normal;
        if (nrm.dot(ray.dir) > 0) nrm = -nrm;
        break;
    }
    case PRIM_CYLINDER: {
        // 局部：中心在 center，轴 +Y，半高 h/2，底半径 r
        rtfx halfh = rt_mul(radius2, RT_HALF);
        RTVec3 oc = ray.origin - center;
        // 侧面：x^2+z^2 = r^2
        rtfx a = rt_mul(ray.dir.x, ray.dir.x) + rt_mul(ray.dir.z, ray.dir.z);
        if (a > 2) {
            rtfx b = rt_mul(oc.x, ray.dir.x) + rt_mul(oc.z, ray.dir.z);
            rtfx c = rt_mul(oc.x, oc.x) + rt_mul(oc.z, oc.z) - rt_mul(radius, radius);
            rtfx disc = rt_mul(b, b) - rt_mul(a, c);
            if (disc >= 0) {
                rtfx s = rt_sqrt(disc);
                rtfx t1 = rt_div(-b - s, a);
                rtfx t2 = rt_div(-b + s, a);
                for (int k = 0; k < 2; k++) {
                    rtfx tt = (k == 0) ? t1 : t2;
                    if (tt < ray.tmin || tt > ray.tmax) continue;
                    RTVec3 p = ray.point_at(tt);
                    rtfx yy = p.y - center.y;
                    if (yy > -halfh && yy < halfh) {
                        if (t < 0 || tt < t) {
                            t = tt;
                            pt = p;
                            nrm = RTVec3(p.x - center.x, 0, p.z - center.z).normalized();
                        }
                    }
                }
            }
        }
        // 顶盖 / 底盖（圆盘）
        for (int k = 0; k < 2; k++) {
            rtfx yy = (k == 0) ? halfh : -halfh;
            RTVec3 cn = RTVec3(0, k == 0 ? RT_ONE : -RT_ONE, 0);
            rtfx tt = rt_ray_disc(ray, center + RTVec3(0, yy, 0), cn, radius);
            if (tt > 0 && (t < 0 || tt < t)) {
                t = tt; pt = ray.point_at(tt); nrm = cn;
            }
        }
        if (t < 0) return false;
        break;
    }
    case PRIM_CONE: {
        // 局部：底在 center.y - h/2（半径 r），顶在 center.y + h/2（半径 0）
        rtfx halfh = rt_mul(radius2, RT_HALF);
        RTVec3 oc = ray.origin - center;
        // 锥面：rho = r*(1 - (y+halfh)/h)，即 (rho*h/r + y + halfh)^2 = ...
        // 用隐式：(x^2+z^2)/(r^2) = ((halfh - y)/h)^2
        rtfx r2 = rt_mul(radius, radius);
        rtfx h2 = rt_mul(radius2, radius2);
        // a = (dx^2+dz^2)/r2 - dy^2/h2
        rtfx dxz = rt_mul(ray.dir.x, ray.dir.x) + rt_mul(ray.dir.z, ray.dir.z);
        rtfx dy2 = rt_mul(ray.dir.y, ray.dir.y);
        rtfx A = rt_div(dxz, r2) - rt_div(dy2, h2);
        rtfx B = rt_div(rt_mul(oc.x, ray.dir.x) + rt_mul(oc.z, ray.dir.z), r2)
               - rt_div(rt_mul(oc.y + halfh, ray.dir.y), h2);
        rtfx C = rt_div(rt_mul(oc.x, oc.x) + rt_mul(oc.z, oc.z), r2)
               - rt_div(rt_mul(oc.y + halfh, oc.y + halfh), h2);
        if (rt_abs(A) > 2) {
            rtfx disc = rt_mul(B, B) - rt_mul(A, C);
            if (disc >= 0) {
                rtfx s = rt_sqrt(disc);
                rtfx t1 = rt_div(-B - s, A);
                rtfx t2 = rt_div(-B + s, A);
                for (int k = 0; k < 2; k++) {
                    rtfx tt = (k == 0) ? t1 : t2;
                    if (tt < ray.tmin || tt > ray.tmax) continue;
                    RTVec3 p = ray.point_at(tt);
                    rtfx yy = p.y - center.y;
                    if (yy > -halfh && yy < halfh) {
                        if (t < 0 || tt < t) {
                            t = tt; pt = p;
                            nrm = RTVec3(p.x - center.x, 0, p.z - center.z).normalized();
                        }
                    }
                }
            }
        }
        // 底盖圆盘
        {
            RTVec3 cn(0, -RT_ONE, 0);
            rtfx tt = rt_ray_disc(ray, center + RTVec3(0, -halfh, 0), cn, radius);
            if (tt > 0 && (t < 0 || tt < t)) { t = tt; pt = ray.point_at(tt); nrm = cn; }
        }
        if (t < 0) return false;
        break;
    }
    case PRIM_TORUS: {
        // 转局部空间（轴 +Y，中心在原点）：射线起点减 center
        RTRay lray(ray.origin - center, ray.dir, ray.tmin, ray.tmax);
        // 先用包围盒粗剪
        RTAABB tb;
        rtfx ext = radius + radius2;
        tb = RTAABB(RTVec3(-ext, -radius2, -ext), RTVec3(ext, radius2, ext));
        rtfx tn, tf;
        if (!rt_ray_aabb(lray, tb, tn, tf)) return false;
        // 步进检测符号变化 + Newton 精化
        rtfx step = rt_div((tb.mx.x - tb.mn.x), rt_itofx(40));
        if (step < 64) step = 64;
        rtfx prev_t = tn;
        rtfx prev_f = torus_implicit(lray.point_at(prev_t), radius, radius2);
        rtfx cur_t = prev_t + step;
        bool found = false;
        while (cur_t <= tf) {
            rtfx f = torus_implicit(lray.point_at(cur_t), radius, radius2);
            if ((prev_f <= 0 && f >= 0) || (prev_f >= 0 && f <= 0)) {
                // Newton 精化
                rtfx a = prev_t, b = cur_t;
                for (int it = 0; it < 8; it++) {
                    rtfx m = rt_div(a + b, rt_itofx(2));
                    rtfx fm = torus_implicit(lray.point_at(m), radius, radius2);
                    if (rt_abs(fm) < 50) { a = b = m; break; }
                    if ((fm <= 0 && prev_f <= 0) || (fm >= 0 && prev_f >= 0)) a = m;
                    else b = m;
                }
                t = rt_div(a + b, rt_itofx(2));
                found = true;
                break;
            }
            prev_t = cur_t; prev_f = f;
            cur_t += step;
        }
        if (!found) return false;
        pt = ray.point_at(t);
        // 法线：对局部点求隐函数梯度
        RTVec3 lp = pt - center;
        rtfx xz2 = rt_mul(lp.x, lp.x) + rt_mul(lp.z, lp.z);
        rtfx rho = rt_sqrt(xz2);
        if (rho < 2) rho = 2;
        rtfx k = rt_div(RT_ONE - rt_div(radius, rho), RT_ONE); // (1 - R/rho)
        nrm = RTVec3(rt_mul(lp.x, k), lp.y, rt_mul(lp.z, k)).normalized();
        break;
    }
    default:
        return false;
    }
    return t > 0;
}

bool prim_hit(const Primitive& p, const RTRay& ray, HitInfo& h) {
    RTVec3 n, pt; RTVec2 uv;
    if (!p.intersect(ray, h.t, n, pt, uv)) return false;
    h.normal = n; h.point = pt; h.uv = uv; h.material = p.material;
    return true;
}

// ---------------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------------
int primitives_self_test() {
    int fail = 0;
    auto near = [&](rtfx got, rtfx want, rtfx tol) {
        if (!rt_near(got, want, tol)) fail++;
    };
    auto V = [](int x, int y, int z) { return RTVec3(rt_itofx(x), rt_itofx(y), rt_itofx(z)); };
    // 1. 球心原点半径 1，射线 +Z 外打向 -Z
    {
        Primitive s = Primitive::make_sphere(RTVec3(0,0,0), RT_ONE, 0);
        RTRay ray(RTVec3(0,0,rt_itofx(5)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = s.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(4), 600);
    }
    // 2. 平面 y=0（n=(0,1,0), d=0），射线从 (0,5,0) 朝下
    {
        Primitive p = Primitive::make_plane(V(0,1,0), 0, 0);
        RTRay ray(RTVec3(0,rt_itofx(5),0), RTVec3(0,-RT_ONE,0));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = p.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(5), 600);
    }
    // 3. 三角形 z=0，射线从 +Z
    {
        Primitive tri = Primitive::make_triangle(
            V(-1,-1,0), V(1,-1,0), V(0,1,0), 0);
        RTRay ray(RTVec3(0,0,rt_itofx(3)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = tri.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(3), 600);
    }
    // 4. 盒 [-1,1]^3，射线穿正面
    {
        Primitive b = Primitive::make_box(
            V(-1,-1,-1), V(1,1,1), 0);
        RTRay ray(RTVec3(0,0,rt_itofx(5)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = b.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(4), 600);
    }
    // 5. 圆盘：中心原点法线 +Z 半径 1，射线从 +Z
    {
        Primitive d = Primitive::make_disc(RTVec3(0,0,0), V(0,0,1), RT_ONE, 0);
        RTRay ray(RTVec3(0,0,rt_itofx(3)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = d.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(3), 600);
    }
    // 6. 圆柱：中心原点 r=1 h=2，射线从 +Z 打侧面
    {
        Primitive c = Primitive::make_cylinder(RTVec3(0,0,0), RT_ONE, rt_itofx(2), 0);
        RTRay ray(RTVec3(rt_itofx(3),0,0), RTVec3(-RT_ONE,0,0));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = c.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;
        near(t, rt_itofx(2), 800);
    }
    // 7. 圆环：中心原点 R=2 r=0.5，射线从 +Z 打大环
    {
        Primitive tor = Primitive::make_torus(RTVec3(0,0,0),
            rt_itofx(2), nefu::fx::fxf(1,2), 0);
        RTRay ray(RTVec3(rt_itofx(3),0,0), RTVec3(-RT_ONE,0,0));
        rtfx t; RTVec3 n, pt; RTVec2 uv;
        bool hit = tor.intersect(ray, t, n, pt, uv);
        if (!hit) fail++;   // 应在 x=R+r=2.5 附近命中
    }
    // 8. 包围盒：球体 bounds 中心应为球心
    {
        Primitive s = Primitive::make_sphere(V(1,2,3), RT_ONE, 0);
        RTAABB bb = s.bounds();
        RTVec3 c = bb.center();
        near(c.x, rt_itofx(1), 300);
        near(c.y, rt_itofx(2), 300);
        near(c.z, rt_itofx(3), 300);
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
