// nefuOS simlib —— 传染病模型实现 + 自测
#include "simlib/epidemic.h"
#include <cmath>
#include <cstdio>

namespace nefu {
namespace simx {

Epidemic::Epidemic(int N_, int I0) : N(N_), beta(0.3), gamma(0.1) {
    if (I0 > N) I0 = N;
    st.S = N - I0;
    st.I = I0;
    st.R = 0;
    peak = I0;
    peak_day_no = 0;
    day = 0;
}

void Epidemic::set_params(double b, double g) {
    beta = b; gamma = g;
    if (beta < 0) beta = 0;
    if (gamma < 0) gamma = 0;
}

SIRState Epidemic::step_day() {
    day++;
    if (st.I == 0 || st.S == 0) return st;
    // 新感染数：与易感人数和感染人数成比例
    int new_inf = (int)(beta * st.S * st.I / (double)N);
    if (new_inf > st.S) new_inf = st.S;
    int new_rec = (int)(gamma * st.I);
    if (new_rec == 0 && gamma > 0 && st.I > 0) new_rec = 1;   // 小感染量也能恢复
    if (new_rec > st.I) new_rec = st.I;
    st.S -= new_inf;
    st.I = st.I + new_inf - new_rec;
    st.R += new_rec;
    if (st.I < 0) st.I = 0;
    if (st.I > peak) { peak = st.I; peak_day_no = day; }
    return st;
}

std::vector<SIRState> Epidemic::run_days(int days) {
    std::vector<SIRState> r;
    for (int i = 0; i < days; i++) {
        step_day();
        r.push_back(st);
    }
    return r;
}

// ---- self test ----
int Epidemic::self_test() {
    int fails = 0;
    // 1. 基本守恒：S+I+R 恒等于 N
    {
        Epidemic e(1000, 5);
        e.set_params(0.3, 0.1);
        for (int i = 0; i < 60; i++) {
            e.step_day();
            if (e.total() != 1000) fails++;
        }
    }
    // 2. 疫情爆发：感染先升后降
    {
        Epidemic e(10000, 10);
        e.set_params(0.4, 0.1);   // R0=4，强传染
        int prev = e.state().I;
        bool rose = false, fell = false;
        for (int i = 0; i < 80; i++) {
            e.step_day();
            int now = e.state().I;
            if (now > prev) rose = true;
            if (now < prev && rose) fell = true;
            prev = now;
        }
        if (!rose || !fell) fails++;
        if (e.peak_infected() < 100) fails++;   // 峰值应有相当规模
    }
    // 3. 无传染（beta=0）：感染逐渐清零，S 不变
    {
        Epidemic e(500, 20);
        e.set_params(0.0, 0.2);
        int s0 = e.state().S;
        for (int i = 0; i < 30; i++) e.step_day();
        if (e.state().I != 0) fails++;
        if (e.state().S != s0) fails++;
        if (e.state().R != 20) fails++;
    }
    // 4. 零感染人口：状态不变
    {
        Epidemic e(100, 0);
        e.set_params(0.5, 0.1);
        for (int i = 0; i < 10; i++) e.step_day();
        if (e.state().I != 0 || e.state().S != 100) fails++;
    }
    // 5. R0 计算
    {
        Epidemic e(1000, 1);
        e.set_params(0.6, 0.2);
        if (std::abs(e.r0() - 3.0) > 1e-9) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
