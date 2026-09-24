// nefuOS 仿真引擎库 —— 流体仿真 (Fluid)
// 包含：
//   - LBM D2Q9 简化格子玻尔兹曼 (9 个离散速度方向)
//   - MAC 网格 (Stable Fluids / Jos Stam 简化)
//   - 速度场 / 密度场
//   - 扩散 / 投影(解泊松) / 平流
//   - 边界条件 (固壁反射)
#pragma once
#include <stdint.h>

namespace nefu {
namespace simulate {

// ============================================================
// LBM D2Q9：二维九速格子玻尔兹曼
//   方向编号 0..8：
//     0=(0,0) 1=(1,0) 2=(0,1) 3=(-1,0) 4=(0,-1)
//     5=(1,1) 6=(-1,1) 7=(-1,-1) 8=(1,-1)
// ============================================================
struct LBMFluid {
    int    w, h;
    double* f;        // 分布函数 w*h*9
    double* f_new;    // 临时
    double* rho;      // 密度场 w*h
    double* ux;       // 宏观速度 x w*h
    double* uy;       // 宏观速度 y w*h
    double  tau;      // 弛豫时间 (粘度 = tau/3 - 1/6)
    double  u0;       // 驱动流速

    void init(int w, int h, double tau);
    void shutdown();
    void reset();
    void set_obstacle(int x, int y, bool on);     // 固体障碍物
    double obstacle(int x, int y) const;
    void step();                                  // 碰撞+流动+边界
    double max_velocity() const;
};

// ============================================================
// Stable Fluids (Jos Stam) 简化 MAC 网格
//   仅速度 + 密度 (染料) 场：
//     diffuse -> project -> advect
// ============================================================
struct StableFluids {
    int   w, h;
    double* u;        // 水平速度 (CELL CENTER)
    double* v;        // 垂直速度
    double* u0;       // 上一帧缓存
    double* v0;
    double* dens;     // 密度/染料
    double* dens0;
    double* curl;     // 涡度约束缓存
    double  visc;     // 粘度
    double  diff;     // 密度扩散率
    int     iter;     // 高斯-赛德尔迭代次数

    void init(int w, int h, double visc, double diff);
    void shutdown();
    void reset();
    void add_source(double* x, const double* s, double dt);
    void add_density(int x, int y, double amount);
    void add_velocity(int x, int y, double ax, double ay);
    void step(double dt);
    double density_at(int x, int y) const;
    double velocity_at(int x, int y, double& vx, double& vy) const;

private:
    void diffuse(int b, double* x, double* x0, double diff, double dt);
    void advect(int b, double* d, const double* d0, const double* u,
                const double* v, double dt);
    void project(double* u, double* v, double* p, double* div);
    void set_bnd(int b, double* x);
};

// ============================================================
// 自检
// ============================================================
int fluid_self_test();

} // namespace simulate
} // namespace nefu
