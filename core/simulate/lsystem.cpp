// nefuOS 仿真引擎库 —— L-系统实现
#include <cstdio>
#include "lsystem.h"
#include <math.h>
#include <string.h>

namespace nefu {
namespace simulate {

static inline int sym_idx(char c) {
    int i = (unsigned char)c - ' ';
    if (i < 0 || i >= LS_MAX_SYMBOLS) return 0;
    return i;
}

// ============================================================
// LSystem
// ============================================================
void LSystem::init() {
    for (int i = 0; i < LS_MAX_SYMBOLS; i++) rules[i][0] = 0;
    axiom[0] = 0;
    angle = 0.5235987755982988;   // 30°
    step_len = 4;
    out_len = 0; out_cap = 4096;
    output = new char[out_cap];
    output[0] = 0;
    rng = 54321;
}
void LSystem::shutdown() {
    delete[] output; output = 0;
}
void LSystem::set_rule(char sym, const char* rule) {
    int i = sym_idx(sym);
    strncpy(rules[i], rule, LS_MAX_RULE_LEN - 1);
    rules[i][LS_MAX_RULE_LEN - 1] = 0;
}
void LSystem::set_axiom(const char* s) {
    strncpy(axiom, s, sizeof(axiom) - 1);
    axiom[sizeof(axiom) - 1] = 0;
}
void LSystem::iterate(int n) {
    // 拷贝公理到 output
    if (out_len == 0) {
        strncpy(output, axiom, out_cap - 1);
        output[out_cap - 1] = 0;
        out_len = (int)strlen(output);
    }
    for (int it = 0; it < n; it++) {
        // 估算新长度：每个字符最多展开到 LS_MAX_RULE_LEN
        // 先做一遍：把 output 展开到临时
        char* tmp = new char[out_cap * 8];
        int tl = 0;
        for (int i = 0; i < out_len; i++) {
            char c = output[i];
            int si = sym_idx(c);
            const char* rep = rules[si];
            if (rep[0] == 0) {
                // 无规则：原样输出
                if (tl < out_cap * 8 - 1) tmp[tl++] = c;
            } else {
                for (int k = 0; rep[k] && tl < out_cap * 8 - 1; k++)
                    tmp[tl++] = rep[k];
            }
        }
        tmp[tl] = 0;
        // 如果超过 out_cap，扩大
        if (tl >= out_cap) {
            int newcap = out_cap * 2;
            while (newcap <= tl) newcap *= 2;
            delete[] output;
            output = new char[newcap];
            out_cap = newcap;
        }
        memcpy(output, tmp, tl + 1);
        out_len = tl;
        delete[] tmp;
    }
}
int LSystem::count_symbol(char c) const {
    int n = 0;
    for (int i = 0; i < out_len; i++) if (output[i] == c) n++;
    return n;
}
void LSystem::preset_koch() {
    set_axiom("F");
    set_rule('F', "F+F--F+F");
    angle = 60.0 * 3.141592653589793 / 180.0;
    step_len = 4;
}
void LSystem::preset_sierpinski() {
    set_axiom("A");
    set_rule('A', "B-A-B");
    set_rule('B', "A+B+A");
    angle = 60.0 * 3.141592653589793 / 180.0;
    step_len = 4;
}
void LSystem::preset_dragon() {
    set_axiom("FX");
    set_rule('X', "X+YF+");
    set_rule('Y', "-FX-Y");
    angle = 90.0 * 3.141592653589793 / 180.0;
    step_len = 4;
}
void LSystem::preset_tree() {
    set_axiom("A");
    set_rule('A', "F[+A][-A]");
    set_rule('F', "FF");
    angle = 25.0 * 3.141592653589793 / 180.0;
    step_len = 3;
}

// ============================================================
// TurtleInterpreter
// ============================================================
void TurtleInterpreter::run(const LSystem& ls) {
    x = 0; y = 0; angle = -1.5707963267948966;   // 朝上
    step = ls.step_len;
    line_count = 0;
    // 简单栈
    TurtleState stk[128]; int sp = 0;
    for (int i = 0; i < ls.out_len; i++) {
        char c = ls.output[i];
        if (c == 'F' || c == 'G') {
            double nx = x + step * cos(angle);
            double ny = y + step * sin(angle);
            if (on_line) on_line(x, y, nx, ny, user);
            line_count++;
            x = nx; y = ny;
        } else if (c == 'f') {
            x += step * cos(angle);
            y += step * sin(angle);
        } else if (c == '+') angle += ls.angle;
        else if (c == '-') angle -= ls.angle;
        else if (c == '[') {
            if (sp < 128) { stk[sp].x = x; stk[sp].y = y; stk[sp].angle = angle; sp++; }
        } else if (c == ']') {
            if (sp > 0) { sp--; x = stk[sp].x; y = stk[sp].y; angle = stk[sp].angle; }
        }
    }
}

// ============================================================
// StochasticLSystem
// ============================================================
void StochasticLSystem::init() {
    for (int i = 0; i < LS_MAX_SYMBOLS; i++) {
        num_alt[i] = 0;
        for (int k = 0; k < MAX_ALT; k++) {
            rules[i][k][0] = 0; probs[i][k] = 0;
        }
    }
    axiom[0] = 0;
    out_len = 0; out_cap = 4096;
    output = new char[out_cap];
    output[0] = 0;
    rng = 999;
}
void StochasticLSystem::shutdown() {
    delete[] output; output = 0;
}
void StochasticLSystem::add_rule(char sym, const char* rule, double p) {
    int i = sym_idx(sym);
    if (num_alt[i] >= MAX_ALT) return;
    int k = num_alt[i]++;
    strncpy(rules[i][k], rule, LS_MAX_RULE_LEN - 1);
    rules[i][k][LS_MAX_RULE_LEN - 1] = 0;
    probs[i][k] = p;
}
void StochasticLSystem::set_axiom(const char* s) {
    strncpy(axiom, s, sizeof(axiom) - 1);
    axiom[sizeof(axiom) - 1] = 0;
}
void StochasticLSystem::iterate(int n) {
    strncpy(output, axiom, out_cap - 1);
    output[out_cap - 1] = 0;
    out_len = (int)strlen(output);
    for (int it = 0; it < n; it++) {
        char* tmp = new char[out_cap * 8];
        int tl = 0;
        for (int i = 0; i < out_len; i++) {
            char c = output[i];
            int si = sym_idx(c);
            int na = num_alt[si];
            if (na == 0) {
                if (tl < out_cap * 8 - 1) tmp[tl++] = c;
                continue;
            }
            // 按概率选规则
            rng = rng * 1664525u + 1013904223u;
            double r = (double)(rng >> 8) / (double)(1u << 24);
            double acc = 0; int pick = na - 1;
            for (int k = 0; k < na; k++) {
                acc += probs[si][k];
                if (r <= acc) { pick = k; break; }
            }
            const char* rep = rules[si][pick];
            for (int k = 0; rep[k] && tl < out_cap * 8 - 1; k++)
                tmp[tl++] = rep[k];
        }
        tmp[tl] = 0;
        if (tl >= out_cap) {
            int newcap = out_cap * 2;
            while (newcap <= tl) newcap *= 2;
            delete[] output;
            output = new char[newcap];
            out_cap = newcap;
        }
        memcpy(output, tmp, tl + 1);
        out_len = tl;
        delete[] tmp;
    }
}
int StochasticLSystem::count_symbol(char c) const {
    int n = 0;
    for (int i = 0; i < out_len; i++) if (output[i] == c) n++;
    return n;
}

// ============================================================
// 自检
// ============================================================
int lsystem_self_test() {
    int fail = 0;

    // --- 1) Koch 曲线：axiom=F, rule F->F+F--F+F, 迭代1次后 F 数=4 ---
    {
        LSystem ls; ls.init();
        ls.preset_koch();
        ls.iterate(1);
        if (ls.count_symbol('F') != 4) fail++;
        // 迭代2次 F 数 = 16
        ls.iterate(1);
        if (ls.count_symbol('F') != 16) fail++;
        ls.shutdown();
    }

    // --- 2) Sierpinski：迭代 n 次 A 数 = 3^n ---
    {
        LSystem ls; ls.init();
        ls.preset_sierpinski();
        ls.iterate(1);
        // axiom=A, A->B-A-B, B->A+B+A
        // 一次后：B-A-B，其中 B 展开... 不，iterate 只对公理展开一次
        // axiom=A -> rule A -> "B-A-B"。所以 B 数=2, A 数=1
        if (ls.count_symbol('B') != 2) fail++;
        ls.shutdown();
    }

    // --- 3) Dragon：迭代1次 FX -> X+YF+，F 数应=1 ---
    {
        LSystem ls; ls.init();
        ls.preset_dragon();
        ls.iterate(1);
        if (ls.count_symbol('F') != 2) fail++;
        ls.shutdown();
    }

    // --- 4) Turtle：跑 Koch 一次应产生 4 条线段 ---
    {
        LSystem ls; ls.init();
        ls.preset_koch();
        ls.iterate(1);
        TurtleInterpreter ti;
        ti.on_line = 0; ti.user = 0;
        ti.run(ls);
        if (ti.line_count != 4) fail++;
        ls.shutdown();
    }

    // --- 5) 随机 L-系统：跑 3 次不崩溃 ---
    {
        StochasticLSystem sl; sl.init();
        sl.set_axiom("F");
        sl.add_rule('F', "FF", 0.5);
        sl.add_rule('F', "F[+F]F", 0.5);
        sl.iterate(3);
        if (sl.count_symbol('F') < 4) fail++;
        sl.shutdown();
    }

    // --- 6) 树：迭代2次有分支符号 ---
    {
        LSystem ls; ls.init();
        ls.preset_tree();
        ls.iterate(2);
        if (ls.count_symbol('[') < 1) fail++;
        ls.shutdown();
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
