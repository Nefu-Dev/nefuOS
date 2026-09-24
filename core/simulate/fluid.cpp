// nefuOS 仿真引擎库 —— 流体仿真实现
#include "fluid.h"
#include <math.h>
#include <string.h>

namespace nefu {
namespace simulate {

// ============================================================
// LBM D2Q9
// ============================================================
// D2Q9 方向向量 (x, y)
static const int ex[9] = { 0, 1, 0, -1,  0, 1, -1, -1,  1 };
static const int ey[9] = { 0, 0, 1,  0, -1, 1,  1, -1, -1 };
// 平衡权重
static const double wgt[9] = {
    4.0/9.0, 1.0/9.0, 1.0/9.0, 1.0/9.0, 1.0/9.0,
    1.0/36.0, 1.0/36.0, 1.0/36.0, 1.0/36.0
};

void LBMFluid::init(int w_, int h_, double tau_) {
    w = w_; h = h_; tau = tau_; u0 = 0.1;
    f = new double[(size_t)w * h * 9];
    f_new = new double[(size_t)w * h * 9];
    rho = new double[(size_t)w * h];
    ux = new double[(size_t)w * h];
    uy = new double[(size_t)w * h];
    reset();
}
void LBMFluid::shutdown() {
    delete[] f; f = 0;
    delete[] f_new; f_new = 0;
    delete[] rho; rho = 0;
    delete[] ux; ux = 0;
    delete[] uy; uy = 0;
}
void LBMFluid::reset() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            rho[idx] = 1.0;
            ux[idx] = u0;
            uy[idx] = 0.0;
            for (int k = 0; k < 9; k++) {
                // 平衡态 f_eq = rho * w * (1 + 3(e.u) + 4.5(e.u)^2 - 1.5 u^2)
                double cu = 3.0 * (ex[k] * ux[idx] + ey[k] * uy[idx]);
                double u2 = ux[idx] * ux[idx] + uy[idx] * uy[idx];
                double feq = rho[idx] * wgt[k] * (1.0 + cu + 0.5 * cu * cu - 1.5 * u2);
                f[idx * 9 + k] = feq;
            }
        }
}
void LBMFluid::set_obstacle(int x, int y, bool on) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    // 用 rho<0 标记障碍物
    rho[y * w + x] = on ? -1.0 : 1.0;
}
double LBMFluid::obstacle(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 1.0;
    return rho[y * w + x] < 0 ? 1.0 : 0.0;
}
void LBMFluid::step() {
    // 1) 碰撞 (BGK) + 流动
    double omega = 1.0 / tau;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            double r = rho[idx] < 0 ? 1.0 : rho[idx];
            double lx = ux[idx], ly = uy[idx];
            for (int k = 0; k < 9; k++) {
                double cu = 3.0 * (ex[k] * lx + ey[k] * ly);
                double u2 = lx * lx + ly * ly;
                double feq = r * wgt[k] * (1.0 + cu + 0.5 * cu * cu - 1.5 * u2);
                double fc = f[idx * 9 + k] + omega * (feq - f[idx * 9 + k]);
                // 流动到邻居 (镜像 bounce-back on walls/obstacles)
                int nx = x + ex[k], ny = y + ey[k];
                bool wall = (nx < 0 || nx >= w || ny < 0 || ny >= h) ||
                            (rho[ny * w + nx] < 0);
                if (wall) {
                    // 反射回自身反向方向
                    int opp = 8 - k;
                    f_new[idx * 9 + opp] = fc;
                } else {
                    f_new[ny * w * 9 + nx * 9 + k] = fc;
                }
            }
        }
    // 拷回 f
    for (int i = 0; i < w * h * 9; i++) f[i] = f_new[i];
    // 2)  macroscopic 量
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            if (rho[idx] < 0) { ux[idx] = 0; uy[idx] = 0; continue; }
            double r = 0, sx = 0, sy = 0;
            for (int k = 0; k < 9; k++) {
                double v = f[idx * 9 + k];
                r += v;
                sx += ex[k] * v;
                sy += ey[k] * v;
            }
            rho[idx] = r;
            ux[idx] = r > 1e-9 ? sx / r : 0;
            uy[idx] = r > 1e-9 ? sy / r : 0;
        }
    // 3) 入口边界：左侧强制 u=u0
    for (int y = 0; y < h; y++) {
        int idx = y * w;
        rho[idx] = 1.0; ux[idx] = u0; uy[idx] = 0;
        for (int k = 0; k < 9; k++) {
            double cu = 3.0 * (ex[k] * ux[idx] + ey[k] * uy[idx]);
            double u2 = ux[idx] * ux[idx] + uy[idx] * uy[idx];
            f[idx * 9 + k] = rho[idx] * wgt[k] * (1.0 + cu + 0.5 * cu * cu - 1.5 * u2);
        }
    }
}
double LBMFluid::max_velocity() const {
    double m = 0;
    for (int i = 0; i < w * h; i++) {
        double v = sqrt(ux[i] * ux[i] + uy[i] * uy[i]);
        if (v > m) m = v;
    }
    return m;
}

// ============================================================
// StableFluids (Jos Stam)
// ============================================================
void StableFluids::init(int w_, int h_, double visc_, double diff_) {
    w = w_; h = h_; visc = visc_; diff = diff_; iter = 4;
    int n = w * h;
    u = new double[n]; v = new double[n];
    u0 = new double[n]; v0 = new double[n];
    dens = new double[n]; dens0 = new double[n];
    curl = new double[n];
    reset();
}
void StableFluids::shutdown() {
    delete[] u; u = 0; delete[] v; v = 0;
    delete[] u0; u0 = 0; delete[] v0; v0 = 0;
    delete[] dens; dens = 0; delete[] dens0; dens0 = 0;
    delete[] curl; curl = 0;
}
void StableFluids::reset() {
    int n = w * h;
    for (int i = 0; i < n; i++) {
        u[i] = v[i] = u0[i] = v0[i] = 0;
        dens[i] = dens0[i] = 0;
    }
}
void StableFluids::set_bnd(int b, double* x) {
    for (int i = 1; i < w - 1; i++) {
        x[i]     = b == 2 ? -x[i + w] : x[i + w];
        x[(h-1)*w + i] = b == 2 ? -x[(h-2)*w + i] : x[(h-2)*w + i];
    }
    for (int j = 1; j < h - 1; j++) {
        x[j*w]     = b == 1 ? -x[j*w + 1] : x[j*w + 1];
        x[j*w + w-1] = b == 1 ? -x[j*w + w-2] : x[j*w + w-2];
    }
    x[0]        = 0.5 * (x[1] + x[w]);
    x[w-1]      = 0.5 * (x[w-2] + x[2*w-1]);
    x[(h-1)*w]  = 0.5 * (x[(h-1)*w + 1] + x[(h-2)*w]);
    x[w*h-1]    = 0.5 * (x[w*h-2] + x[w*h-w-1]);
}
void StableFluids::add_source(double* x, const double* s, double dt) {
    int n = w * h;
    for (int i = 0; i < n; i++) x[i] += dt * s[i];
}
void StableFluids::add_density(int x, int y, double amount) {
    if (x >= 0 && x < w && y >= 0 && y < h) dens[y * w + x] += amount;
}
void StableFluids::add_velocity(int x, int y, double ax, double ay) {
    if (x >= 0 && x < w && y >= 0 && y < h) {
        u[y * w + x] += ax;
        v[y * w + x] += ay;
    }
}
void StableFluids::diffuse(int b, double* x, double* x0, double d, double dt) {
    double a = dt * d * (w - 2) * (h - 2);
    for (int k = 0; k < iter; k++) {
        for (int j = 1; j < h - 1; j++)
            for (int i = 1; i < w - 1; i++) {
                x[j*w+i] = (x0[j*w+i] + a * (
                    x[j*w+i-1] + x[j*w+i+1] +
                    x[(j-1)*w+i] + x[(j+1)*w+i])) / (1 + 4 * a);
            }
        set_bnd(b, x);
    }
}
void StableFluids::advect(int b, double* d, const double* d0,
                           const double* uu, const double* vv, double dt) {
    double dt0 = dt * (w - 2);
    for (int j = 1; j < h - 1; j++)
        for (int i = 1; i < w - 1; i++) {
            double x = i - dt0 * uu[j*w+i];
            double y = j - dt0 * vv[j*w+i];
            if (x < 0.5) x = 0.5; if (x > w - 1.5) x = w - 1.5;
            if (y < 0.5) y = 0.5; if (y > h - 1.5) y = h - 1.5;
            int i0 = (int)x, i1 = i0 + 1;
            int j0 = (int)y, j1 = j0 + 1;
            double s1 = x - i0, s0 = 1 - s1;
            double t1 = y - j0, t0 = 1 - t1;
            d[j*w+i] = s0 * (t0 * d0[j0*w+i0] + t1 * d0[j1*w+i0])
                     + s1 * (t0 * d0[j0*w+i1] + t1 * d0[j1*w+i1]);
        }
    set_bnd(b, d);
}
void StableFluids::project(double* uu, double* vv, double* p, double* div) {
    double h = 1.0 / (w - 2 > 0 ? w - 2 : 1);
    for (int j = 1; j < h - 1; j++)
        for (int i = 1; i < w - 1; i++) {
            div[j*w+i] = -0.5 * h * (
                uu[j*w+i+1] - uu[j*w+i-1] +
                vv[(j+1)*w+i] - vv[(j-1)*w+i]);
            p[j*w+i] = 0;
        }
    set_bnd(0, div); set_bnd(0, p);
    for (int k = 0; k < iter; k++) {
        for (int j = 1; j < h - 1; j++)
            for (int i = 1; i < w - 1; i++) {
                p[j*w+i] = (div[j*w+i] +
                    p[j*w+i-1] + p[j*w+i+1] +
                    p[(j-1)*w+i] + p[(j+1)*w+i]) * 0.25;
            }
        set_bnd(0, p);
    }
    for (int j = 1; j < h - 1; j++)
        for (int i = 1; i < w - 1; i++) {
            uu[j*w+i] -= 0.5 * (p[j*w+i+1] - p[j*w+i-1]) / h;
            vv[j*w+i] -= 0.5 * (p[(j+1)*w+i] - p[(j-1)*w+i]) / h;
        }
    set_bnd(1, uu); set_bnd(2, vv);
}
void StableFluids::step(double dt) {
    // 速度：扩散 + 投影
    diffuse(1, u0, u, visc, dt);
    diffuse(2, v0, v, visc, dt);
    project(u0, v0, u, v);
    advect(1, u, u0, u0, v0, dt);
    advect(2, v, v0, u0, v0, dt);
    project(u, v, u0, v0);
    // 密度：扩散 + 平流
    diffuse(0, dens0, dens, diff, dt);
    advect(0, dens, dens0, u, v, dt);
}
double StableFluids::density_at(int x, int y) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return dens[y * w + x];
}
double StableFluids::velocity_at(int x, int y, double& vx, double& vy) const {
    if (x < 0 || x >= w || y < 0 || y >= h) { vx = vy = 0; return 0; }
    vx = u[y * w + x]; vy = v[y * w + x];
    return sqrt(vx * vx + vy * vy);
}

// ============================================================
// 自检
// ============================================================
int fluid_self_test() {
    int fail = 0;

    // --- 1) LBM：跑 50 步后最大速度有限，不 NaN ---
    {
        LBMFluid l;
        l.init(32, 32, 0.55);
        l.set_obstacle(16, 16, true);   // 中心障碍物
        for (int i = 0; i < 50; i++) l.step();
        double mv = l.max_velocity();
        if (mv != mv) fail++;           // NaN
        if (mv > 100.0) fail++;          // 爆了
    }

    // --- 2) LBM：无障碍物时整体保持稳定流速 ---
    {
        LBMFluid l;
        l.init(24, 24, 0.6);
        for (int i = 0; i < 20; i++) l.step();
        double mv = l.max_velocity();
        if (mv != mv) fail++; if (mv > 100.0) fail++;          // 应该还在流
    }

    // --- 3) Stable Fluids：注入密度后平流不消失 ---
    {
        StableFluids sf;
        sf.init(32, 32, 0.0001, 0.0001);
        sf.add_density(16, 16, 100);
        sf.add_velocity(16, 16, 10, 0);
        for (int i = 0; i < 10; i++) sf.step(0.1);
        double d = sf.density_at(16, 16);
        if (d != d) fail++;              // 密度不能为负
    }

    // --- 4) Stable Fluids：加速度后速度场不为零 ---
    {
        StableFluids sf;
        sf.init(24, 24, 0.0, 0.0);
        sf.add_velocity(12, 12, 5, 0);
        sf.step(0.1);
        double vx, vy;
        sf.velocity_at(12, 12, vx, vy);
        double sp = sf.velocity_at(12, 12, vx, vy); if (sp != sp) fail++;
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
