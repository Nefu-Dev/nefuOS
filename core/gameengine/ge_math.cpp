// ge_math.cpp —— 引擎数学实现：缓动函数 + 自测
#include "ge_math.h"



namespace nefu {
namespace gameengine {

// ============================================================================
//  缓动函数实现（全部 Q16.16 定点）
//  约定：t 已被夹取到 [0,1]，返回进度也在 [0,1] 附近（back/elastic 可越界）
// ============================================================================

// 二次方
static fix ease_in_quad(fix t)  { return fx::fx_mul(t, t); }
static fix ease_out_quad(fix t) { return fx::fx_mul(t, fx::itofix(2) - t); }
static fix ease_inout_quad(fix t) {
    fix two = fx::itofix(2);
    if (t < fx::FX_HALF) return fx::fx_mul(t, fx::fx_mul(t, two));        // 2t^2
    fix u = fx::FX_ONE - t;
    return fx::FX_ONE - fx::fx_mul(fx::itofix(2), fx::fx_mul(u, u));      // 1-2(1-t)^2
}

// 三次方
static fix ease_in_cubic(fix t)  { return fx::fx_mul(fx::fx_mul(t, t), t); }
static fix ease_out_cubic(fix t) {
    fix u = fx::FX_ONE - t;
    return fx::FX_ONE - fx::fx_mul(fx::fx_mul(u, u), u);
}
static fix ease_inout_cubic(fix t) {
    if (t < fx::FX_HALF) {
        return fx::fx_mul(fx::itofix(4), fx::fx_mul(fx::fx_mul(t, t), t)); // 4t^3
    }
    fix u = fx::FX_ONE - t;
    return fx::FX_ONE - fx::fx_mul(fx::itofix(4), fx::fx_mul(fx::fx_mul(u, u), u));
}

// 正弦：用 1 - cos(t * pi/2) 等（定点三角）
static fix ease_in_sine(fix t) {
    // 1 - cos(t * pi/2)
    fix a = fx::fx_mul(t, fx::FX_PI_2);
    return fx::FX_ONE - fx::fx_cos(a);
}
static fix ease_out_sine(fix t) {
    // sin(t * pi/2)
    return fx::fx_sin(fx::fx_mul(t, fx::FX_PI_2));
}
static fix ease_inout_sine(fix t) {
    // -(cos(pi*t) - 1)/2
    fix c = fx::fx_cos(fx::fx_mul(t, fx::FX_PI));
    return fx::fx_mul(fx::FX_ONE - c, fx::FX_HALF);
}

// 指数：2^(10(t-1)) 等（用 fx_exp）
static fix ease_in_expo(fix t) {
    if (t == 0) return 0;
    // 10*(t-1)
    fix x = fx::fx_mul(fx::itofix(10), t - fx::FX_ONE);
    return fx::fx_exp(x);
}
static fix ease_out_expo(fix t) {
    if (t == fx::FX_ONE) return fx::FX_ONE;
    // 1 - 2^(-10t)
    fix x = fx::fx_mul(fx::itofix(-10), t);
    return fx::FX_ONE - fx::fx_exp(x);
}
static fix ease_inout_expo(fix t) {
    if (t == 0) return 0;
    if (t == fx::FX_ONE) return fx::FX_ONE;
    if (t < fx::FX_HALF) {
        fix x = fx::fx_mul(fx::itofix(20), t - fx::FX_HALF);  // 20(t-0.5)
        return fx::fx_mul(fx::FX_HALF, fx::fx_exp(x));
    }
    fix x = fx::fx_mul(fx::itofix(-20), t - fx::FX_HALF);
    return fx::FX_ONE - fx::fx_mul(fx::FX_HALF, fx::fx_exp(x));
}

// back：s=1.70158，轻微过冲
static fix ease_in_back(fix t) {
    fix s = fx::fxf(170158, 100000);   // 1.70158
    fix t2 = fx::fx_mul(t, t);
    fix c = s + fx::FX_ONE;
    return fx::fx_mul(fx::fx_mul(c, t2), t) - fx::fx_mul(s, t2);
}
static fix ease_out_back(fix t) {
    fix s = fx::fxf(170158, 100000);
    fix u = t - fx::FX_ONE;
    fix c = s + fx::FX_ONE;
    return fx::FX_ONE + fx::fx_mul(fx::fx_mul(c, fx::fx_mul(u, u)), u) + fx::fx_mul(s, fx::fx_mul(u, u));
}
static fix ease_inout_back(fix t) {
    fix c1 = fx::fxf(170158, 100000);            // 1.70158
    fix c2 = fx::fx_mul(c1, fx::fxf(1525, 1000)); // c1*1.525
    fix two = fx::itofix(2);
    if (t < fx::FX_HALF) {
        fix u = fx::fx_mul(t, two);                       // 2t
        fix inner = fx::fx_mul((c2 + fx::FX_ONE), u) - c2;
        return fx::fx_mul(fx::FX_HALF, fx::fx_mul(fx::fx_mul(u, u), inner));
    }
    fix u = fx::fx_mul(t, two) - two;                     // 2t-2
    fix inner = fx::fx_mul((c2 + fx::FX_ONE), u) + c2;
    return fx::fx_mul(fx::FX_HALF,
                      fx::fx_mul(fx::fx_mul(u, u), inner) + two);
}

// bounce：分段衰减反弹（经典四区间）
static fix ease_out_bounce(fix t) {
    fix n1 = fx::fxf(75625, 10000);   // 7.5625
    // 四个反弹区间的分界点
    fix c1 = fx::fxf(1, 27);          // 1/2.75
    fix c2 = fx::fxf(2, 27);          // 2/2.75
    fix c3 = fx::fxf(25, 27);         // 2.5/2.75
    if (t < c1) {
        return fx::fx_mul(n1, fx::fx_mul(t, t));
    } else if (t < c2) {
        fix u = t - c1;
        return fx::fx_mul(n1, fx::fx_mul(u, u)) + fx::fxf(2, 27);
    } else if (t < c3) {
        fix u = t - c2;
        return fx::fx_mul(n1, fx::fx_mul(u, u)) + fx::fxf(9196, 10000); // ~0.9196
    }
    fix u = t - c3;
    return fx::fx_mul(n1, fx::fx_mul(u, u)) + fx::fxf(9844, 10000);     // ~0.9844
}

// elastic：正弦衰减振荡
static fix ease_out_elastic(fix t) {
    if (t == 0) return 0;
    if (t == fx::FX_ONE) return fx::FX_ONE;
    fix p = fx::fxf(3, 10);            // 周期 0.3
    fix s = fx::fx_div(p, fx::itofix(4));
    fix u = t - fx::FX_ONE;
    // 包络：e^{-10 * ln2 * t} ≈ 2^{-10t}，用 fx_exp 近似（ln2 已在常量表）
    fix env = fx::fx_exp(fx::fx_mul(fx::itofix(-10), fx::fx_mul(fx::FX_LN2, t)));
    fix ang = fx::fx_div(u - s, p);
    ang = fx::fx_mul(ang, fx::FX_2PI);
    return fx::FX_ONE + fx::fx_mul(env, fx::fx_sin(ang));
}

fix easing(EaseType type, fix t) {
    t = ge_clamp(t, 0, fx::FX_ONE);
    switch (type) {
    case EASE_LINEAR:        return t;
    case EASE_IN_QUAD:       return ease_in_quad(t);
    case EASE_OUT_QUAD:      return ease_out_quad(t);
    case EASE_INOUT_QUAD:    return ease_inout_quad(t);
    case EASE_IN_CUBIC:      return ease_in_cubic(t);
    case EASE_OUT_CUBIC:     return ease_out_cubic(t);
    case EASE_INOUT_CUBIC:   return ease_inout_cubic(t);
    case EASE_IN_SINE:       return ease_in_sine(t);
    case EASE_OUT_SINE:      return ease_out_sine(t);
    case EASE_INOUT_SINE:    return ease_inout_sine(t);
    case EASE_IN_EXPO:       return ease_in_expo(t);
    case EASE_OUT_EXPO:      return ease_out_expo(t);
    case EASE_INOUT_EXPO:    return ease_inout_expo(t);
    case EASE_IN_BACK:       return ease_in_back(t);
    case EASE_OUT_BACK:      return ease_out_back(t);
    case EASE_INOUT_BACK:    return ease_inout_back(t);
    case EASE_OUT_BOUNCE:    return ease_out_bounce(t);
    case EASE_OUT_ELASTIC:   return ease_out_elastic(t);
    default:                 return t;
    }
}

const char* easing_name(EaseType type) {
    switch (type) {
    case EASE_LINEAR:      return "Linear";
    case EASE_IN_QUAD:     return "InQuad";
    case EASE_OUT_QUAD:    return "OutQuad";
    case EASE_INOUT_QUAD:  return "InOutQuad";
    case EASE_IN_CUBIC:    return "InCubic";
    case EASE_OUT_CUBIC:   return "OutCubic";
    case EASE_INOUT_CUBIC: return "InOutCubic";
    case EASE_IN_SINE:     return "InSine";
    case EASE_OUT_SINE:    return "OutSine";
    case EASE_INOUT_SINE:  return "InOutSine";
    case EASE_IN_EXPO:     return "InExpo";
    case EASE_OUT_EXPO:    return "OutExpo";
    case EASE_INOUT_EXPO:  return "InOutExpo";
    case EASE_IN_BACK:     return "InBack";
    case EASE_OUT_BACK:    return "OutBack";
    case EASE_INOUT_BACK:  return "InOutBack";
    case EASE_OUT_BOUNCE:  return "OutBounce";
    case EASE_OUT_ELASTIC: return "OutElastic";
    default:               return "?";
    }
}

// ============================================================================
//  自测
// ============================================================================
// 用一个宽松的相等比较：定点有误差，允许 ±0.02 (≈1310)
static bool approx_eq(fix a, fix b, fix tol = fx::fxf(2, 100)) {
    return fx::fx_abs(a - b) <= tol;
}

int ge_math_self_test() {
    int fails = 0;

    // --- Vec2 基本运算 ---
    Vec2 a(fx::itofix(3), fx::itofix(4));
    Vec2 b(fx::itofix(6), fx::itofix(8));
    Vec2 c = a + b;
    if (c.x != fx::itofix(9) || c.y != fx::itofix(12)) { fails++; }

    // 点积：3*6 + 4*8 = 50
    if (!approx_eq(a.dot(b), fx::itofix(50), fx::fxf(1,100))) fails++;

    // 长度：sqrt(9+16)=5
    if (!approx_eq(a.len(), fx::itofix(5), fx::fxf(2,100))) fails++;

    // 归一化：(3,4)/5 = (0.6,0.8)
    Vec2 n = a.normalized();
    if (!approx_eq(n.x, fx::fxf(6,10), fx::fxf(2,100))) fails++;
    if (!approx_eq(n.y, fx::fxf(8,10), fx::fxf(2,100))) fails++;

    // 距离：(0,0)-(3,4)=5
    if (!approx_eq(a.dist(Vec2(0,0)), fx::itofix(5), fx::fxf(2,100))) fails++;

    // 旋转 90 度：(1,0) -> (0,1)
    Vec2 ux(fx::FX_ONE, 0);
    Vec2 r90 = ux.rotated(fx::FX_PI_2);
    if (!approx_eq(r90.x, 0, fx::fxf(3,100))) fails++;
    if (!approx_eq(r90.y, fx::FX_ONE, fx::fxf(3,100))) fails++;

    // --- Mat3 ---
    Mat3 tr = Mat3::make_translate(fx::itofix(10), fx::itofix(20));
    Vec2 tp = tr.transform_point(Vec2(fx::itofix(1), fx::itofix(2)));
    if (tp.x != fx::itofix(11) || tp.y != fx::itofix(22)) fails++;

    Mat3 sc = Mat3::make_scale(fx::itofix(2), fx::itofix(3));
    Vec2 sp = sc.transform_point(Vec2(fx::itofix(5), fx::itofix(7)));
    if (sp.x != fx::itofix(10) || sp.y != fx::itofix(21)) fails++;

    // 复合：先缩放 2x，再平移 (10,20)
    Mat3 comb = Mat3::mul(tr, sc);
    Vec2 cp = comb.transform_point(Vec2(fx::itofix(5), fx::itofix(7)));
    // (5*2+10, 7*3+20) = (20, 41)
    if (cp.x != fx::itofix(20) || cp.y != fx::itofix(41)) fails++;

    // 矩阵逆：平移矩阵的逆应把点移回原点
    Mat3 inv = tr.inverse();
    Vec2 back = inv.transform_point(Vec2(fx::itofix(11), fx::itofix(22)));
    if (!approx_eq(back.x, fx::itofix(1), fx::fxf(3,100))) fails++;
    if (!approx_eq(back.y, fx::itofix(2), fx::fxf(3,100))) fails++;

    // --- Transform2D ---
    Transform2D t2d;
    t2d.position = Vec2(fx::itofix(100), fx::itofix(50));
    t2d.scale = Vec2(fx::itofix(2), fx::itofix(2));
    Mat3 w = t2d.to_matrix();
    Vec2 wp = w.transform_point(Vec2(fx::itofix(10), fx::itofix(5)));
    // (100+20, 50+10) = (120, 60)
    if (!approx_eq(wp.x, fx::itofix(120), fx::fxf(3,100))) fails++;
    if (!approx_eq(wp.y, fx::itofix(60), fx::fxf(3,100))) fails++;

    // --- Quat ---
    Quat q = Quat::identity();
    if (!approx_eq(q.w, fx::FX_ONE, fx::fxf(1,100))) fails++;
    Quat q2 = Quat::slerp(q, q, fx::FX_HALF);
    if (!approx_eq(q2.w, fx::FX_ONE, fx::fxf(5,100))) fails++;

    // --- lerp ---
    if (!approx_eq(lerp(fx::itofix(0), fx::itofix(100), fx::FX_HALF),
                   fx::itofix(50), fx::fxf(2,100))) fails++;

    // --- easing 端点：所有缓动在 t=0 -> 0，t=1 -> 1（除 elastic 边界） ---
    for (int i = 0; i < EASE_COUNT; i++) {
        EaseType et = (EaseType)i;
        fix e0 = easing(et, 0);
        fix e1 = easing(et, fx::FX_ONE);
        if (!approx_eq(e0, 0, fx::fxf(5,100)) && i != EASE_OUT_ELASTIC) fails++;
        // t=1 时大多数应为 1（bounce/elastic 也应回到 1）
        if (!approx_eq(e1, fx::FX_ONE, fx::fxf(8,100))) fails++;
    }

    // linear 应为恒等
    if (!approx_eq(easing(EASE_LINEAR, fx::fxf(3,10)), fx::fxf(3,10), fx::fxf(1,100))) fails++;

    // ease_out_quad(0.5) = 1-(1-0.5)^2 = 0.75
    if (!approx_eq(easing(EASE_OUT_QUAD, fx::FX_HALF), fx::fxf(75,100), fx::fxf(3,100))) fails++;

    // 名字表能取到
    for (int i = 0; i < EASE_COUNT; i++) {
        const char* nm = easing_name((EaseType)i);
        if (!nm || nm[0] == '?') fails++;
    }


    // --- Color ---
    Color cc1(0, 0, 0, 255), cc2(255, 255, 255, 255);
    Color cmid = Color::lerp(cc1, cc2, fx::FX_HALF);
    if (cmid.r < 120 || cmid.r > 135) fails++;
    Color cred(255, 0, 0);
    Color cdark = cred.scale(128);
    if (cdark.r < 120 || cdark.r > 135) fails++;
    uint32_t cpk = cred.pack();
    Color cback = Color::unpack(cpk);
    if (cback.r != 255 || cback.g != 0 || cback.b != 0) fails++;

    // --- Mat3 求逆 ---
    Mat3 mt = Mat3::make_translate(fx::itofix(100), fx::itofix(50));
    Mat3 minv = mat3_inverse(mt);
    Mat3 midm = Mat3::mul(mt, minv);
    Vec2 mpt = midm.transform_point(Vec2(fx::itofix(30), fx::itofix(20)));
    if (fx::fixtoi(mpt.x) != 30 || fx::fixtoi(mpt.y) != 20) fails++;

    // --- TransformStack ---
    TransformStack ts;
    ts.push();
    ts.mul(Mat3::make_translate(fx::itofix(10), 0));
    Vec2 tsp = ts.current().transform_point(Vec2(0,0));
    if (fx::fixtoi(tsp.x) != 10) fails++;
    ts.pop();
    tsp = ts.current().transform_point(Vec2(0,0));
    if (fx::fixtoi(tsp.x) != 0) fails++;

    // --- Vec2 投影/反射/距离 ---
    Vec2 rv(fx::itofix(100), 0);
    Vec2 rn(0, fx::itofix(-1));
    Vec2 rr = vec_reflect(rv, rn);
    if (fx::fixtoi(rr.x) != 100) fails++;
    Vec2 rup(0, fx::itofix(100));
    Vec2 rr2 = vec_reflect(rup, rn);
    if (fx::fixtoi(rr2.y) != -100) fails++;
    // 颜色插值
    uint32_t c1 = Color(0,0,0).pack();
    uint32_t c2 = Color(255,255,255).pack();
    uint32_t midc = lerp_color(c1, c2, fx::FX_HALF);
    Color mc = Color::unpack(midc);
    if (mc.r < 120 || mc.r > 135) fails++;



    // --- MathUtils ---
    if (fx_clamp(fx::itofix(100), fx::itofix(0), fx::itofix(50)) != fx::itofix(50)) fails++;
    if (fx_clamp(fx::itofix(-10), fx::itofix(0), fx::itofix(50)) != fx::itofix(0)) fails++;
    if (int_clamp(100, 0, 50) != 50) fails++;
    fix m = fx_lerp(fx::itofix(0), fx::itofix(100), fx::FX_HALF);
    if (fx::fixtoi(m) != 50) fails++;
    fix s = fx_smooth(fx::itofix(0), fx::itofix(100), fx::itofix(2), fx::fxf(1,60));
    if (s <= 0 || s >= fx::itofix(100)) fails++;
    return fails;
}

} // namespace gameengine
} // namespace nefu
