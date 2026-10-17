// nefuOS tiny chatbot -- core/apps/chatbot.cpp
// ----------------------------------------------------------------------------
// an intentionally MINIMAL conversational AI: no reasoning, no inference, no
// learned weights. it is a plain keyword->template rule table (an Eliza-style
// pattern matcher) plus a tiny round-robin picker so answers feel alive but
// never "think". GUI: a chat window with wrapped message history on top and a
// text input line at the bottom (ASCII + IME UTF-8 both work).
// keys:
//   printable / IME text   type into the current line
//   Backspace              delete one char
//   Enter                  send; the bot replies instantly
//   Esc                    close window
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../lib/llm.h"
#include "../lib/softmath.h"

namespace nefu {
namespace tinybot {

// ---- real neural model (nefuLLM), loaded lazily once ----
// The whole TinyStories-33M int8 blob + tokenizer workspace + activations
// are kept alive for the app's lifetime. If loading fails (no model disk /
// file), the app falls back to the rule table below.
static nefu::llm::Model* g_llm  = 0;
static uint8_t*          g_blob = 0;
static void*             g_ws   = 0;
static size_t            g_ws_sz = 0;
static void*             g_sc   = 0;
static size_t            g_sc_sz = 0;

static bool llm_try_load() {
    if (g_llm) return true;
    uint8_t* blob = 0; uint32_t blob_sz = 0;
    if (!nefu::platform_llm_load(&blob, &blob_sz)) return false;
    size_t ws = nefu::llm::llm_workspace_needed(blob, blob_sz);
    void* wp = nefu::kalloc(ws ? ws : 1);
    if (!wp) { nefu::kfree(blob); return false; }
    nefu::llm::Model* m = 0;
    if (nefu::llm::llm_load(blob, blob_sz, wp, ws, &m) != 0) {
        nefu::kfree(blob); nefu::kfree(wp); return false;
    }
    size_t sc = nefu::llm::llm_scratch_needed(m);
    void* sp = nefu::kalloc(sc ? sc : 1);
    if (!sp) { nefu::kfree(blob); nefu::kfree(wp); return false; }
    g_blob = blob; g_ws = wp; g_ws_sz = ws; g_sc = sp; g_sc_sz = sc; g_llm = m;
    return true;
}

// True when `text` is pure ASCII/English (the neural model is English-only;
// CJK/IME input keeps the Chinese rule table).
static bool is_ascii(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        if (*p >= 0x80) return false;
    return true;
}

// ---- palette (soft chat look) ----
const uint32_t COL_BG     = 0x00FAF9F6;   // near-white background
const uint32_t COL_USER   = 0x001F6FEB;   // user bubble text (blue)
const uint32_t COL_BOT    = 0x0026A269;   // bot bubble text (green)
const uint32_t COL_DIM    = 0x0090A4B8;   // hint / meta text
const uint32_t COL_PROMPT = 0x00333D4C;   // input prompt
const uint32_t COL_INPUT  = 0x00222A35;   // typed text
const int      WIN_W = 460, WIN_H = 400;
const int      LINE_H = 15;               // pixel height of one line
const int      MAX_MSG = 96;              // ring-buffer message cap (bounds memory)

// ---- tuning bar: two sliders right under the header (content coords) ----
const int      SL_TOP  = 24;              // bar top (right under the header strip)
const int      SL_ROWH = 24;              // one slider row height
const int      SL_X0   = 78;              // track left
const int      SL_X1   = 336;             // track right
const int      SL_VX   = 344;             // value text x
const int      SL_TRACKY = 7;             // track y offset inside a row
const int      SL_TRACKH = 5;             // track thickness
const int      SL_KNOB_W = 10, SL_KNOB_H = 14;
const int      SL_TEMP_MIN = 10, SL_TEMP_MAX = 200;   // temperature 0.10 ~ 2.00
const int      SL_REP_MIN  = 100, SL_REP_MAX = 200;   // repetition penalty 1.00 ~ 2.00

static void draw_slider(Surface& s, int row_y, const char* label, int v_cs,
                        int vmin, int vmax, uint32_t col);

// ---- message ring buffer (fixed size, no unbounded growth) ----
struct Msg {
    char text[256];
    bool bot;                 // true = bot reply, false = user line
};

struct ChatState {
    Msg msgs[MAX_MSG];
    int head, count;          // ring indices
    char input[256];
    int  input_len;
    int  rule_turn[64];       // round-robin pickers per matched rule
    int  temp_cs;             // generation temperature, centi-units (80 = 0.80)
    int  rep_cs;              // repetition penalty, centi-units (130 = 1.30)

    ChatState() : head(0), count(0), input_len(0), temp_cs(80), rep_cs(130) {
        for (int i = 0; i < MAX_MSG; i++) msgs[i].text[0] = 0;
        for (int i = 0; i < 64; i++) rule_turn[i] = 0;
        input[0] = 0;
    }

    void add(const char* s, bool bot) {
        Msg* m = &msgs[(head + count) % MAX_MSG];
        int n = (int)strlen(s);
        if (n > 255) n = 255;
        memcpy(m->text, s, (size_t)n);
        m->text[n] = 0;
        m->bot = bot;
        if (count < MAX_MSG) count++;
        else head = (head + 1) % MAX_MSG;   // overwrite the oldest
    }

    void on_char(char c) {
        if (input_len < (int)sizeof(input) - 1) {
            input[input_len++] = c;
            input[input_len] = 0;
        }
    }
    void on_ime(const char* u) {           // committed IME UTF-8 (CJK etc.)
        int n = (int)strlen(u);
        int room = (int)sizeof(input) - 1 - input_len;
        if (n > room) n = room;
        if (n > 0) {
            memcpy(input + input_len, u, (size_t)n);
            input_len += n;
            input[input_len] = 0;
        }
    }
    void on_backspace() {
        if (input_len <= 0) return;
        // step back a full UTF-8 sequence (so CJK deletes cleanly)
        input_len--;
        while (input_len > 0 && ((unsigned char)input[input_len] & 0xC0) == 0x80)
            input_len--;
        input[input_len] = 0;
    }

    // ---- tiny "model": keyword -> template table ----
    struct Rule { const char* key; const char* const* out; int nout; };
    static const char* const R_HELLO[3];
    static const char* const R_WHO[3];
    static const char* const R_MAKER[3];
    static const char* const R_OS[3];
    static const char* const R_HELP[3];
    static const char* const R_FEEL[3];
    static const char* const R_THANKS[2];
    static const char* const R_BYE[3];
    static const char* const R_JOKE[2];
    static const char* const R_FALLBACK[5];

    static const Rule RULES[34];
    static const int  RULE_COUNT = 34;

    // ASCII-only lowercase of 'in' (Chinese passthrough unchanged)
    static void lower_ascii(const char* in, char* out, int outsz) {
        int i = 0;
        while (in[i] && i < outsz - 1) {
            char c = in[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            out[i++] = c;
        }
        out[i] = 0;
    }

    void reply_to(const char* text) {
        // real neural model for English/ASCII input (kept alive once loaded);
        // Chinese / rule-missing input falls back to the keyword table.
        if (is_ascii(text) && llm_try_load()) {
            static char gen[512];
            // 滑块参数（centi-units -> Q16.16）写进模型，本次生成即生效
            nefu::llm::llm_set_sampling(g_llm, fx::fxf(temp_cs, 100),
                                        fx::fxf(rep_cs, 100));
            int n = nefu::llm::llm_generate(g_llm, text, gen, (int)sizeof(gen),
                                            g_sc, g_sc_sz, 48);
            if (n > 0) { add(gen, true); return; }
        }
        char buf[256];
        lower_ascii(text, buf, sizeof(buf));
        for (int r = 0; r < RULE_COUNT; r++) {
            if (strstr(buf, RULES[r].key)) {
                int t = rule_turn[r] % RULES[r].nout;
                rule_turn[r]++;
                add(RULES[r].out[t], true);
                return;
            }
        }
        // fallback: no rule hit
        int t = rule_turn[RULE_COUNT] % 5;
        rule_turn[RULE_COUNT]++;
        add(R_FALLBACK[t], true);
    }

    void submit() {
        if (input_len == 0) {
            add("(没有文字。说点什么，比如：你好)", true);
            return;
        }
        add(input, false);
        reply_to(input);
        input[0] = 0;
        input_len = 0;
    }

    // utf-8-aware line count when wrapped at maxw px
    static int wrap_lines(const char* s, int maxw) {
        int lines = 1;
        int w = 0;
        const char* p = s;
        while (*p) {
            int cw = ((unsigned char)*p < 0x80) ? 8 : 16;
            if (w + cw > maxw && w > 0) { lines++; w = 0; }
            w += cw;
            if ((unsigned char)*p < 0x80) p++;
            else {
                unsigned char b = (unsigned char)*p;
                int n = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
                p += n;
            }
        }
        return lines;
    }

    static int draw_wrapped(Surface& s, int x, int y, int maxw, const char* text,
                            uint32_t fg, uint32_t bg) {
        const char* p = text;
        while (*p) {
            int w = 0;
            const char* start = p;
            while (*p) {
                int cw = ((unsigned char)*p < 0x80) ? 8 : 16;
                if (w + cw > maxw && p > start) break;
                w += cw;
                if ((unsigned char)*p < 0x80) p++;
                else {
                    unsigned char b = (unsigned char)*p;
                    int n = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
                    p += n;
                }
            }
            char tmp[300];
            int len = (int)(p - start);
            if (len > 299) len = 299;
            memcpy(tmp, start, (size_t)len);
            tmp[len] = 0;
            gfx::text(s, x, y, tmp, fg, bg);
            y += LINE_H;
        }
        return y;
    }

    void paint(Surface& s) {
        s.fill(COL_BG);
        int W = s.width, H = s.height;
        const int M = 8;
        int maxw = W - 2 * M;

        // header strip
        gfx::fillrect(s, 0, 0, W, 22, 0x002E3A46);
        gfx::text(s, M, 4, "nefu TinyBot — 内置真实模型（68M，英文推理）", 0xFFFFFFFF, 0x002E3A46);

        // tuning bar: 温度 / 重复惩罚 两个滑块
        gfx::fillrect(s, 0, 22, W, SL_TOP + 2 * SL_ROWH - 22, 0x00EEF1F5);
        draw_slider(s, SL_TOP + 2, "温度", temp_cs, SL_TEMP_MIN, SL_TEMP_MAX, 0x001F6FEB);
        draw_slider(s, SL_TOP + 2 + SL_ROWH, "重复惩罚", rep_cs, SL_REP_MIN, SL_REP_MAX, 0x0026A269);

        // input line at the bottom
        int in_y = H - 26;
        gfx::fillrect(s, 0, in_y - 2, W, 28, 0x00EEF1F5);
        char inbuf[270];
        ksprintf(inbuf, sizeof(inbuf), "> %s_", input);
        gfx::text(s, M, in_y + 5, inbuf, COL_INPUT, 0x00EEF1F5);

        // chat area: show the latest messages that fit
        int top = SL_TOP + 2 * SL_ROWH;
        int avail = in_y - 2 - top;
        // total wrapped height
        int total = 0;
        for (int i = 0; i < count; i++) {
            int idx = (head + i) % MAX_MSG;
            total += wrap_lines(msgs[idx].text, maxw) * LINE_H;
        }
        int skip = total > avail ? total - avail : 0;
        int y = top - skip;
        for (int i = 0; i < count; i++) {
            int idx = (head + i) % MAX_MSG;
            uint32_t fg = msgs[idx].bot ? COL_BOT : COL_USER;
            y = draw_wrapped(s, M, y, maxw, msgs[idx].text, fg, COL_BG);
        }
    }
};

} // namespace tinybot
} // namespace nefu

// ---- the "model": keyword -> reply templates (the whole intelligence) ----
namespace nefu { namespace tinybot {

const char* const ChatState::R_HELLO[3] = {
    "你好！我是 TinyBot。英文输入走本地真实模型，中文走内置规则。",
    "嗨～ 我能陪你聊几句，但真的没脑子。",
    "哈喽！关键词匹配，命中即答。",
};
const char* const ChatState::R_WHO[3] = {
    "我叫 TinyBot，nefuOS 内置的最小聊天 AI。",
    "名字？TinyBot。模型极小：一张规则表而已。",
    "我是 TinyBot，没有思考能力，只认关键词。",
};
const char* const ChatState::R_MAKER[3] = {
    "我是别人写进 nefuOS 的一小段 C++，作者就是你看着的这位开发者。",
    "谁做的？一小段规则匹配代码，没什么了不起。",
    "创建我的人没有给我大脑，只给了关键词表。",
};
const char* const ChatState::R_OS[3] = {
    "我住在 nefuOS 里，一个 C++ 写的小型桌面操作系统。",
    "nefuOS——两个后端：Windows 宿主 + 裸机内核。",
    "我是 nefuOS 的原生应用之一。",
};
const char* const ChatState::R_HELP[3] = {
    "试试对我说：你好 / 你是谁 / 讲个笑话 / 再见。",
    "我能做的：认关键词、回模板句。仅此而已。",
    "输入中文或英文都行，命中规则我就答。",
};
const char* const ChatState::R_FEEL[3] = {
    "挺好的！没思考也就没烦恼。",
    "我很稳定，因为没有情绪。",
    "状态：在线。模型：极小。",
};
const char* const ChatState::R_THANKS[2] = {
    "不客气！(虽然我没做什么)",
    "收到，举手之劳。",
};
const char* const ChatState::R_BYE[3] = {
    "再见！按 Esc 可关掉我。",
    "拜拜，别想我——我也不会想你。",
    "回见！希望下次你还能找得到我。",
};
const char* const ChatState::R_JOKE[2] = {
    "笑话：我是最省电的 AI，因为我根本不“想”。",
    "程序员笑话：0 和 1 开会，1 对 0 说：你没我有份量。",
};
const char* const ChatState::R_FALLBACK[5] = {
    "唔，没听懂。不过没关系——我本来就没有思考能力。",
    "这个话题超出我的规则表啦。换点我能“命中”的？",
    "我是极小模型，只认得几个关键词。",
    "嗯嗯（假装懂了）。你继续说说？",
    "我的“大脑”就一张表，能答的有限。",
};

const ChatState::Rule ChatState::RULES[34] = {
    { "who are you", R_WHO, 3 },
    { "你是谁",       R_WHO, 3 },
    { "你叫什么",     R_WHO, 3 },
    { "名字",         R_WHO, 3 },
    { "谁做",         R_MAKER, 3 },
    { "谁创建",       R_MAKER, 3 },
    { "作者",         R_MAKER, 3 },
    { "created by",   R_MAKER, 3 },
    { "help",         R_HELP, 3 },
    { "帮助",         R_HELP, 3 },
    { "能做什么",     R_HELP, 3 },
    { "你会什么",     R_HELP, 3 },
    { "how are you",  R_FEEL, 3 },
    { "好吗",         R_FEEL, 3 },
    { "怎么样",       R_FEEL, 3 },
    { "thank",        R_THANKS, 2 },
    { "谢谢",         R_THANKS, 2 },
    { "感谢",         R_THANKS, 2 },
    { "bye",          R_BYE, 3 },
    { "再见",         R_BYE, 3 },
    { "拜拜",         R_BYE, 3 },
    { "退出",         R_BYE, 3 },
    { "joke",         R_JOKE, 2 },
    { "笑话",         R_JOKE, 2 },
    { "讲个",         R_JOKE, 2 },
    { "hello",        R_HELLO, 3 },
    { "hi",           R_HELLO, 3 },
    { "你好",         R_HELLO, 3 },
    { "嗨",           R_HELLO, 3 },
    { "哈喽",         R_HELLO, 3 },
    { "nefuos",       R_OS, 3 },
    { "nefu",         R_OS, 3 },
    { "操作系统",     R_OS, 3 },
    { "系统",         R_OS, 3 },
};

}} // namespace

// ---- window glue (mirrors the MiniLang REPL app) ----
using namespace nefu;
using namespace nefu::tinybot;

static ChatState* bot_of(Window* w) { return (ChatState*)w->userdata; }

namespace nefu { namespace tinybot {
// draw one labeled slider row; v_cs is the value in centi-units
static void draw_slider(Surface& s, int row_y, const char* label, int v_cs,
                        int vmin, int vmax, uint32_t col) {
    const uint32_t bg = 0x00EEF1F5;
    gfx::text(s, 8, row_y + 4, label, COL_DIM, bg);
    int ty = row_y + SL_TRACKY;
    int span = SL_X1 - SL_X0;
    int kx = SL_X0 + (span * (v_cs - vmin)) / (vmax - vmin);
    gfx::fillrect(s, SL_X0, ty, span, SL_TRACKH, 0x00D5DCE3);              // track
    if (kx > SL_X0) gfx::fillrect(s, SL_X0, ty, kx - SL_X0, SL_TRACKH, col); // filled part
    gfx::fillrect(s, kx - SL_KNOB_W / 2, ty - (SL_KNOB_H - SL_TRACKH) / 2,
                  SL_KNOB_W, SL_KNOB_H, col);                              // knob
    char vbuf[16];
    ksprintf(vbuf, sizeof(vbuf), "%d.%02d", v_cs / 100, v_cs % 100);
    gfx::text(s, SL_VX, row_y + 4, vbuf, col, bg);
}
}} // namespace nefu::tinybot

// click / drag on a track sets the knob value (window-local content coords)
static void bot_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    ChatState* b = bot_of(w);
    if (!b) return;
    int span = SL_X1 - SL_X0;
    int row1 = SL_TOP + 2;
    int row2 = SL_TOP + 2 + SL_ROWH;
    if (my >= row1 && my < row1 + SL_ROWH) {
        int v = SL_TEMP_MIN + ((mx - SL_X0) * (SL_TEMP_MAX - SL_TEMP_MIN)) / span;
        if (v < SL_TEMP_MIN) v = SL_TEMP_MIN;
        if (v > SL_TEMP_MAX) v = SL_TEMP_MAX;
        b->temp_cs = v;
    } else if (my >= row2 && my < row2 + SL_ROWH) {
        int v = SL_REP_MIN + ((mx - SL_X0) * (SL_REP_MAX - SL_REP_MIN)) / span;
        if (v < SL_REP_MIN) v = SL_REP_MIN;
        if (v > SL_REP_MAX) v = SL_REP_MAX;
        b->rep_cs = v;
    }
}

static void bot_paint(Window* w) {
    bot_of(w)->paint(w->back);
}

static void bot_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    ChatState* b = bot_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->keycode == KEY_ENTER) { b->submit(); return; }
    if (e->keycode == KEY_BACKSPACE) { b->on_backspace(); return; }
    if (e->utf8[0]) { b->on_ime(e->utf8); return; }      // committed CJK / IME text
    char c = e->ascii;
    if (c >= 32 && c < 127) b->on_char(c);
}

static void bot_close(Window* w) {
    ChatState* b = (ChatState*)w->userdata;
    if (b) delete b;
    w->userdata = 0;
}

void nefu::chatbot_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("TinyBot", x, y, WIN_W, WIN_H);
    if (!w) return;
    ChatState* b = new ChatState();
    b->add("我是 TinyBot，nefuOS 内置聊天 AI：英文走本地真实模型，中文走内置规则。", true);
    b->add("用英文问（如 Once upon a time / What is a cat?）会由 68M 神经网络推理作答。", true);
    w->userdata = b;
    w->on_paint = bot_paint;
    w->on_key = bot_key;
    w->on_mouse = bot_mouse;
    w->on_close = bot_close;
    g_wm->raise(w);
}
