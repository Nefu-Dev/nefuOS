// nefuOS 仿真引擎库 —— L-系统 (Lindenmayer System)
// 包含：
//   - 公理 / 产生式规则 / 迭代字符串生成
//   - 海龟图形 (前进/左转/右转/入栈/出栈)
//   - 2D / 3D 扩展 (roll/pitch/yaw)
//   - 随机 L-系统 (同符号多规则按概率选)
//   - 参数 L-系统简化 (数值表达式占位)
//   - 经典例子：Koch 曲线 / Sierpinski 三角 / Dragon 曲线 / 植物
#pragma once
#include <stdint.h>

namespace nefu {
namespace simulate {

// L-系统规则表容量（两个结构共用）
const int LS_MAX_SYMBOLS = 96;   // ' '..'~'
const int LS_MAX_RULE_LEN = 256;

// ============================================================
// 海龟状态
// ============================================================
struct TurtleState {
    double x, y;        // 位置
    double angle;       // 朝向 (弧度)
};

struct TurtleState3D {
    double x, y, z;
    double yaw, pitch, roll;
};

// ============================================================
// L-系统
// ============================================================
struct LSystem {
    // 规则：每个字符 c 映射到一个字符串
    char rules[LS_MAX_SYMBOLS][LS_MAX_RULE_LEN];
    char axiom[128];
    double angle;          // 海龟转角(弧度)
    double step_len;       // 步长
    int    out_len;
    int    out_cap;
    char*  output;        // 迭代后的字符串
    uint32_t rng;

    void init();
    void shutdown();
    void set_rule(char sym, const char* rule);
    void set_axiom(const char* s);
    void iterate(int n);       // 迭代 n 次
    int  count_symbol(char c) const;
    // 预设
    void preset_koch();        // Koch 曲线
    void preset_sierpinski();  // Sierpinski 三角
    void preset_dragon();      // Dragon 曲线
    void preset_tree();        // 植物
};

// ============================================================
// 2D 海龟解释器：把 L-system output 跑成线段列表
//   F/G = 前进并画线
//   f   = 前进不画线
//   +   = 左转 angle
//   -   = 右转 angle
//   [   = 入栈
//   ]   = 出栈
//   X/Y = 无操作 (仅生成)
// ============================================================
struct TurtleInterpreter {
    double x, y, angle;
    double step;
    // 线段回调：由外部提供
    void (*on_line)(double x0, double y0, double x1, double y1, void* user);
    void* user;
    int line_count;

    void run(const LSystem& ls);
};

// ============================================================
// 随机 L-系统：同一符号多规则
// ============================================================
struct StochasticLSystem {
    static const int MAX_ALT = 4;
    char rules[LS_MAX_SYMBOLS][MAX_ALT][LS_MAX_RULE_LEN];
    double probs[LS_MAX_SYMBOLS][MAX_ALT];
    int    num_alt[LS_MAX_SYMBOLS];
    char   axiom[128];
    int    out_len;
    int    out_cap;
    char*  output;
    uint32_t rng;

    void init();
    void shutdown();
    void add_rule(char sym, const char* rule, double p);
    void set_axiom(const char* s);
    void iterate(int n);
    int  count_symbol(char c) const;
};

// ============================================================
// 自检
// ============================================================
int lsystem_self_test();

} // namespace simulate
} // namespace nefu
