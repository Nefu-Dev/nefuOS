// nefuOS simlib —— 传染病传播模型 epidemic
// 教学版：SIR 经典流行病学模型（易感 S / 感染 I / 恢复 R）。
// 支持按人群离散模拟与按微分方程连续模拟两种视角，中文注释。
#pragma once
#include <vector>

namespace nefu {
namespace simx {

// SIR 模型状态快照
struct SIRState {
    int S;   // 易感人数
    int I;   // 感染人数
    int R;   // 恢复人数
    SIRState() : S(0), I(0), R(0) {}
};

// 传染病 SIR 模拟器（离散事件式：每天更新一次）
class Epidemic {
public:
    // 构造：总人口 N，初始感染 I0，其余易感
    Epidemic(int N, int I0);

    // 设置模型参数：传染率 beta（每个易感-感染接触的传播概率），
    // 恢复率 gamma（每日恢复比例）
    void set_params(double beta, double gamma);

    // 推进一天，返回当天结束状态
    SIRState step_day();
    // 模拟若干天，返回每天结束状态序列
    std::vector<SIRState> run_days(int days);

    // 当前状态
    SIRState state() const { return st; }
    int total() const { return st.S + st.I + st.R; }

    // 峰值感染人数与出现天数（需先 run_days 或 step 记录）
    int peak_infected() const { return peak; }
    int peak_day() const { return peak_day_no; }

    // 基本再生数 R0 = beta/gamma（简化口径）
    double r0() const { return gamma > 0 ? beta / gamma : 0; }

    // ---- self test ----
    static int self_test();

private:
    int N;
    double beta, gamma;
    SIRState st;
    int peak, peak_day_no;
    int day;
};

} // namespace simx
} // namespace nefu
