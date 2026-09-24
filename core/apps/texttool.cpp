// nefuOS 文本工具 —— 两段文本的快速对比与分析
// 输入两段文本(s1/s2)，选择一个操作，结果即时显示：
//   1) 编辑距离 (Levenshtein)
//   2) 最长公共子序列 LCS
//   3) 相似度 (0..1)
//   4) 词频统计 (对 s1)
// 操作完全由 nefu::textproc 库完成；本文件只负责窗口与键盘交互。
//   Tab : 切换 s1/s2 输入框   Enter : 计算   1..4 : 选操作   Esc : 关闭
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../textproc/textproc_all.h"

namespace nefu {
namespace {

const int TW_W = 620, TW_H = 400;

struct TextTool {
    char s1[160];
    char s2[160];
    int  active;          // 0 = 正在编辑 s1, 1 = s2
    int  op;             // 0..3
    char result[480];

    TextTool() : active(0), op(0) {
        s1[0] = s2[0] = result[0] = 0;
    }

    char* cur_buf() { return active == 0 ? s1 : s2; }
    int   cur_cap() { return 159; }

    void recompute() {
        using namespace textproc;
        switch (op) {
        case 0: {
            int d = levenshtein(s1, s2);
            ksprintf(result, sizeof(result), "Levenshtein edit distance = %d", d);
            break;
        }
        case 1: {
            char* l = lcs_string(s1, s2);
            ksprintf(result, sizeof(result), "LCS (len %d): %s", lcs_length(s1, s2), l);
            delete[] l;
            break;
        }
        case 2: {
            float r = similarity_ratio(s1, s2);
            ksprintf(result, sizeof(result), "Similarity = %.3f", (double)r);
            break;
        }
        default: {
            WordFreq* wf = word_freq(s1);
            char* rep = word_freq_report(wf);
            ksprintf(result, sizeof(result), "Word freq(s1):\n%s", rep);
            delete[] rep;
            word_freq_free(wf);
            break;
        }
        }
    }

    void paint(Surface& surf) {
        surf.fill(0x00FAF8EF);
        gfx::text_scale(surf, 12, 10, "Text Tool - compare two strings", 0x00776756, 0x00FAF8EF, 2);
        // 操作菜单
        const char* names[4] = {"1 EditDist", "2 LCS", "3 Similarity", "4 WordFreq"};
        for (int i = 0; i < 4; i++) {
            uint32_t fg = (op == i) ? 0x00FFFFFF : 0x00505050;
            uint32_t bg = (op == i) ? 0x002E86C1 : 0x00ECDFCC;
            gfx::fillrect(surf, 12 + i * 148, 44, 140, 24, bg);
            gfx::text(surf, 20 + i * 148, 50, names[i], fg, bg);
        }
        // s1 框
        gfx::text(surf, 12, 86, "Text 1 (s1):", 0x00505050, 0x00FAF8EF);
        gfx::fillrect(surf, 12, 104, TW_W - 24, 26, active == 0 ? 0x00D6EAF8 : 0x00FFFFFF);
        gfx::rect(surf, 12, 104, TW_W - 24, 26, active == 0 ? 0x002E86C1 : 0x00BBADA0);
        gfx::text(surf, 18, 111, s1, 0x00202020, active == 0 ? 0x00D6EAF8 : 0x00FFFFFF);
        // s2 框
        gfx::text(surf, 12, 142, "Text 2 (s2):", 0x00505050, 0x00FAF8EF);
        gfx::fillrect(surf, 12, 160, TW_W - 24, 26, active == 1 ? 0x00D6EAF8 : 0x00FFFFFF);
        gfx::rect(surf, 12, 160, TW_W - 24, 26, active == 1 ? 0x002E86C1 : 0x00BBADA0);
        gfx::text(surf, 18, 167, s2, 0x00202020, active == 1 ? 0x00D6EAF8 : 0x00FFFFFF);
        // 结果区
        gfx::text(surf, 12, 200, "Result:", 0x0027AE60, 0x00FAF8EF);
        gfx::fillrect(surf, 12, 218, TW_W - 24, 130, 0x00FFFFFF);
        gfx::rect(surf, 12, 218, TW_W - 24, 130, 0x00BBADA0);
        // 结果可能多行，逐行画
        char line[8][64];
        int rows = 0;
        {
            int li = 0;
            for (int i = 0; result[i] && rows < 8; i++) {
                if (result[i] == '\n') { line[rows][li] = 0; rows++; li = 0; }
                else if (li < 63) line[rows][li++] = result[i];
            }
            if (li > 0 || rows == 0) { line[rows][li] = 0; rows++; }
        }
        for (int r = 0; r < rows && r < 6; r++)
            gfx::text(surf, 20, 226 + r * 18, line[r], 0x00202020, 0x00FFFFFF);
        gfx::text(surf, 12, TW_H - 16, "Tab: switch input   Enter: run   1-4: op   Esc: close",
                  0x00909090, 0x00FAF8EF);
    }
};

static TextTool* tool_of(Window* w) { return (TextTool*)w->userdata; }

static void tt_paint(Window* w) { tool_of(w)->paint(w->back); }

static void tt_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    TextTool* t = tool_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->keycode == KEY_TAB) { t->active ^= 1; return; }
    if (e->keycode == KEY_ENTER) { t->recompute(); return; }
    if (e->keycode == KEY_BACKSPACE) {
        char* b = t->cur_buf();
        int L = (int)strlen(b);
        if (L > 0) b[L - 1] = 0;
        return;
    }
    if (e->ascii >= '1' && e->ascii <= '4') { t->op = e->ascii - '1'; t->recompute(); return; }
    char c = e->ascii;
    if (c >= 32 && c < 127) {
        char* b = t->cur_buf();
        int L = (int)strlen(b);
        if (L < t->cur_cap()) { b[L] = c; b[L + 1] = 0; }
    }
}

static void tt_close(Window* w) {
    if (w->userdata) delete (TextTool*)w->userdata;
    w->userdata = 0;
}

} // namespace

void texttool_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Text Tool", x, y, TW_W, TW_H);
    if (!w) return;
    TextTool* t = new TextTool();
    t->recompute();
    w->userdata = t;
    w->on_paint = tt_paint;
    w->on_key = tt_key;
    w->on_close = tt_close;
    g_wm->raise(w);
}

} // namespace nefu
