// nefu::minilang —— 图形化 REPL 窗口（core/apps/repl.cpp）
// ----------------------------------------------------------------------------
// 一个最简的交互式迷你语言终端：顶部是输出历史，底部是输入行。
// 按键：
//   可打印字符  输入到当前行
//   Backspace   删除一个字符
//   Enter       提交当前行，在持久全局环境里求值并打印结果
//   Esc         关闭窗口
// 全局环境在窗口存活期间一直保留，因此可以像真 REPL 一样连续定义变量与函数。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../minilang/minilang_all.h"

namespace nefu {
namespace minilang_repl {

using namespace nefu::minilang;

// 输出颜色
const uint32_t COL_BG     = 0x001E1E2E;   // 背景（深蓝黑）
const uint32_t COL_FG     = 0x00CDD6F4;   // 正文
const uint32_t COL_PROMPT = 0x00A6E3A1;   // 提示符（绿）
const uint32_t COL_ERR    = 0x00F38BA8;   // 错误（红）
const uint32_t COL_DIM    = 0x006C7086;   // 暗淡提示
const int      REPL_W = 640, REPL_H = 420;
const int      LINE_H = 14;               // 每行像素高
const int      MAX_LINES = 40;            // 历史缓冲行数

// REPL 会话状态
struct ReplState {
    Env*  global;                 // 持久全局环境（内置 + 标准库已注册）
    char  input[256];             // 当前输入行
    int   input_len;
    List<String> history;          // 输出历史（含输入回显）

    ReplState() : global(0), input_len(0) {
        input[0] = 0;
    }

    void init() {
        global = env_new(0);
        builtin_register_all(global);
        stdlib_register_all(global);
        history.push(String("nefu minilang REPL  (输入表达式，Esc 退出)"));
        history.push(String("内置: print len range type str int abs max min sum sort"));
    }

    void push_line(const char* s, uint32_t /*color*/) {
        history.push(String(s));
        // 保留最近 MAX_LINES 行
        while (history.size() > MAX_LINES) {
            // List 没有头部删除接口，用 erase_all 不行；这里只截断上限
            break;
        }
    }

    // 把一个运行时值格式化成可读字符串，写入 out（自带 new[] 缓冲）
    static void value_to_str(Value v, String& out) {
        char buf[64];
        switch (v.type) {
        case V_NULL:  out = String("null"); break;
        case V_BOOL:  out = String(v.i ? "true" : "false"); break;
        case V_INT:   ksprintf(buf, sizeof(buf), "%lld", (long long)v.i); out = String(buf); break;
        case V_FX: {
            // Q16.16 定点转十进制（整数.小数）
            fx::fix f = (fx::fix)v.i;
            int64_t whole = fx::fixtoi(f);
            int64_t frac  = (f < 0 ? -f : f) & 0xFFFF;
            frac = frac * 1000 / 65536;
            ksprintf(buf, sizeof(buf), "%lld.%03lld", (long long)whole, (long long)frac);
            out = String(buf);
            break;
        }
        case V_OBJ: {
            Obj* o = v.obj;
            if (!o) { out = String("null"); break; }
            if (o->otype == O_STRING) { out = String(((StrObj*)o)->s); }
            else if (o->otype == O_ARRAY) { out = String("[array]"); }
            else if (o->otype == O_CLOSURE) { out = String("[fn]"); }
            else if (o->otype == O_BUILTIN) { out = String("[builtin]"); }
            else out = String("?");
            break;
        }
        default: out = String("?"); break;
        }
    }

    // 提交当前输入行
    void submit() {
        // 回显输入
        char echo[320];
        ksprintf(echo, sizeof(echo), ">>> %s", input);
        push_line(echo, COL_PROMPT);

        String err;
        Value r = minilang_run(input, global, &err);
        if (val_is_null(r)) {
            push_line(err.c_str(), COL_ERR);
        } else {
            String s;
            value_to_str(r, s);
            push_line(s.c_str(), COL_FG);
            val_drop(r);
        }
        input[0] = 0;
        input_len = 0;
    }

    void on_char(char c) {
        if (c == '\n' || c == '\r') { submit(); return; }
        if (input_len < (int)sizeof(input) - 1) {
            input[input_len++] = c;
            input[input_len] = 0;
        }
    }

    void on_backspace() {
        if (input_len > 0) { input_len--; input[input_len] = 0; }
    }

    void paint(Surface& s) {
        s.fill(COL_BG);
        int W = s.width;
        int y = 8;
        int start = history.size() > MAX_LINES ? history.size() - MAX_LINES : 0;
        for (int i = start; i < history.size(); i++) {
            const char* line = history[i].c_str();
            uint32_t col = COL_FG;
            if (line[0] == '>') col = COL_PROMPT;
            else if (line[0] == 'e' && line[1] == 'r') col = COL_ERR;
            gfx::text(s, 8, y, line, col, COL_BG);
            y += LINE_H;
        }
        // 输入行
        char buf[300];
        ksprintf(buf, sizeof(buf), ">>> %s_", input);
        gfx::text(s, 8, s.height - 24, buf, COL_PROMPT, COL_BG);
        gfx::text(s, 8, s.height - 12, "Esc: close", COL_DIM, COL_BG);
    }
};

} // namespace minilang_repl
} // namespace nefu

// ---- 窗口胶水 ----
using namespace nefu;
using namespace nefu::minilang_repl;

static ReplState* repl_of(Window* w) { return (ReplState*)w->userdata; }

static void repl_paint(Window* w) {
    repl_of(w)->paint(w->back);
}

static void repl_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    ReplState* r = repl_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->keycode == KEY_ENTER) { r->submit(); return; }
    if (e->keycode == KEY_BACKSPACE) { r->on_backspace(); return; }
    char c = e->ascii;
    if (c >= 32 && c < 127) r->on_char(c);
}

static void repl_close(Window* w) {
    ReplState* r = (ReplState*)w->userdata;
    if (r) {
        if (r->global) env_release(r->global);
        delete r;
    }
    w->userdata = 0;
}

void minilang_repl_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("MiniLang REPL", x, y, REPL_W, REPL_H);
    if (!w) return;
    ReplState* r = new ReplState();
    r->init();
    w->userdata = r;
    w->on_paint = repl_paint;
    w->on_key = repl_key;
    w->on_close = repl_close;
    g_wm->raise(w);
}
