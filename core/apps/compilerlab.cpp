// ============================================================================
// nefuOS 迷你编译器实验室（compilerlab.cpp）
// ----------------------------------------------------------------------------
// 输入一段 C 子集代码，一键走完整工具链：
//   预处理 -> 词法(token) -> 语法(AST) -> 三地址 IR -> x86-64 汇编
// 窗口内分页显示各阶段产物。
//   上下方向键 / PgUp PgDn : 滚动输出
//   R                       : 用内置样例重新编译
//   ESC                     : 关闭窗口
// 不依赖任何外部进程，全部在 nefu::compiler 内完成。
// ============================================================================
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../compiler/compiler_all.h"

namespace nefu {

namespace {

const int LAB_W = 640, LAB_H = 460;

// 内置样例：一个带循环与函数调用的 C 子集程序
const char* LAB_SAMPLE =
    "#define N 10\n"
    "int sum_to(int n){\n"
    "    int i = 0;\n"
    "    int s = 0;\n"
    "    while (i < n) {\n"
    "        s = s + i;\n"
    "        i = i + 1;\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "int main(void){\n"
    "    int x = sum_to(N);\n"
    "    if (x > 20) { x = x - 1; }\n"
    "    return x;\n"
    "}\n";

// 第二个内置样例：带宏与 do-while 的程序
const char* LAB_SAMPLE2 =
    "#define A 3\n"
    "#define B 5\n"
    "int main(void){\n"
    "    int s = 0;\n"
    "    int i = 0;\n"
    "    do { s = s + A; i = i + 1; } while (i < B);\n"
    "    if (s > 10) { s = s - 1; } else { s = s + 1; }\n"
    "    return s;\n"
    "}\n";

// 第三个内置样例：嵌套条件与函数调用
const char* LAB_SAMPLE3 =
    "int max2(int a, int b){ if (a > b) { return a; } return b; }\n"
    "int main(void){\n"
    "    int x = max2(4, 9);\n"
    "    int y = 0;\n"
    "    for (int i = 0; i < 3; i = i + 1) { y = y + i; }\n"
    "    return x + y;\n"
    "}\n";

const char* LAB_SAMPLES[3] = { LAB_SAMPLE, LAB_SAMPLE2, LAB_SAMPLE3 };

struct Lab {
    String report;      // 流水线产物文本
    int scroll;         // 顶部行
    int errs;
    int sample;         // 当前样例编号

    const char* cur() { return LAB_SAMPLES[sample % 3]; }

    void rebuild() {
        report.clear();
        errs = compiler::compiler_pipeline(cur(), report);
        scroll = 0;
    }
};

} // namespace

static Lab* lab_of(Window* w) { return (Lab*)w->userdata; }

static void lab_paint(Window* w) {
    Lab* L = lab_of(w);
    Surface& s = w->back;
    s.fill(0x001E1E2E);
    gfx::text_scale(s, 10, 8, "Mini Compiler Lab", 0x00F9E2AF, 0x001E1E2E, 2);
    char status[96];
    ksprintf(status, sizeof(status), "errors=%d  sample=%d   [1/2/3] switch  [R] rebuild  [Esc] close",
             L->errs, L->sample);
    gfx::text(s, 10, 34, status, 0x00A6E3A1, 0x001E1E2E);

    // 按行绘制 report（简单线性扫描）
    int y = 56;
    int line = 0;
    int rlen = L->report.len();
    int i = 0;
    while (i < rlen && y < s.height - 16) {
        // 取一行
        int start = i;
        while (i < rlen && L->report[i] != '\n') i++;
        int len = i - start;
        if (i < rlen) i++; // 跳过 '\n'
        if (line >= L->scroll) {
            // 截断过长行
            if (len > 96) len = 96;
            char buf[100];
            for (int k = 0; k < len; k++) buf[k] = L->report[start + k];
            buf[len] = 0;
            uint32_t fg = 0x00CDD6F4;
            if (buf[0] == '=') fg = 0x00F9E2AF;        // 阶段标题
            else if (buf[0] == '.') fg = 0x00FAB387;   // 汇编标签
            else if (buf[0] == 'v' && buf[1] >= '0' && buf[1] <= '9') fg = 0x0089DCEB;
            gfx::text(s, 12, y, buf, fg, 0x001E1E2E);
            y += 13;
        }
        line++;
    }

    // 底部信息栏：滚动位置 / 报告行数
    char foot[96];
    int total_lines = 0;
    for (int k = 0; k < L->report.len(); k++) if (L->report[k] == '\n') total_lines++;
    ksprintf(foot, sizeof(foot), "scroll=%d/%d  sample=%d/3  errors=%d",
             L->scroll, total_lines, L->sample, L->errs);
    gfx::text(s, 10, s.height - 16, foot, 0x0089DCEB, 0x001E1E2E);
}

static void lab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    Lab* L = lab_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->ascii == 'r' || e->ascii == 'R') { L->rebuild(); return; }
    if (e->ascii == '1') { L->sample = 0; L->rebuild(); return; }
    if (e->ascii == '2') { L->sample = 1; L->rebuild(); return; }
    if (e->ascii == '3') { L->sample = 2; L->rebuild(); return; }
    // 简单滚动：上/下方向键（keycode 值由平台定义，这里用 ascii 兜底）
    if (e->ascii == 'j' || e->ascii == 'J') L->scroll += 3;
    if (e->ascii == 'k' || e->ascii == 'K') { L->scroll -= 3; if (L->scroll < 0) L->scroll = 0; }
}

static void lab_close(Window* w) {
    if (w->userdata) delete (Lab*)w->userdata;
    w->userdata = 0;
}

void compilerlab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Compiler Lab", x, y, LAB_W, LAB_H);
    if (!w) return;
    Lab* L = new Lab();
    L->rebuild();
    w->userdata = L;
    w->on_paint = lab_paint;
    w->on_key = lab_key;
    w->on_close = lab_close;
    g_wm->raise(w);
}

} // namespace nefu
