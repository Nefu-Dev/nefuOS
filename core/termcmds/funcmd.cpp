// =============================================================================
//  funcmd.cpp —— 趣味命令实现
// =============================================================================
#include "funcmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  自动换行
// -----------------------------------------------------------------------------
void word_wrap(const char* text, int width, nefu::List<nefu::String>& out) {
    if (!text) return;
    nefu::String line;
    const char* p = text;
    while (*p) {
        // 取一个词
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        int wlen = (int)(p - start);
        if (wlen == 0) break;
        int need = line.len() == 0 ? wlen : line.len() + 1 + wlen;
        if (need > width && line.len() > 0) {
            out.push(line);
            line.clear();
        } else if (line.len() > 0) {
            line += ' ';
        }
        for (int i = 0; i < wlen; i++) line += start[i];
    }
    if (line.len() > 0) out.push(line);
    if (out.empty()) out.push(nefu::String(""));
}

// -----------------------------------------------------------------------------
//  5x7 点阵字模(每行 5 bit, bit4 在最左)
// -----------------------------------------------------------------------------
static const uint8_t FONT[][7] = {
    // A
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    // B
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    // C
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    // D
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    // F
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    // G
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    // H
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    // I
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    // J
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    // K
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    // L
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    // M
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    // N
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    // O
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    // P
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    // Q
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    // R
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    // S
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    // T
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    // U
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    // V
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    // W
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    // X
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    // Y
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    // Z
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static const uint8_t* glyph_for(char c) {
    if (c >= 'A' && c <= 'Z') return FONT[c - 'A'];
    if (c >= 'a' && c <= 'z') return FONT[c - 'a'];
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    return blank;
}

void figlet_render(const char* text, nefu::List<nefu::String>& out) {
    if (!text) return;
    for (int row = 0; row < 7; row++) {
        nefu::String line;
        for (int i = 0; text[i]; i++) {
            const uint8_t* g = glyph_for(text[i]);
            for (int col = 4; col >= 0; col--) {
                line += (g[row] & (1 << col)) ? '#' : ' ';
            }
            line += ' '; // 字间距
        }
        out.push(line);
    }
}

// -----------------------------------------------------------------------------
//  语料
// -----------------------------------------------------------------------------
static const char* g_fortunes[] = {
    "The best way to predict the future is to invent it. — Alan Kay",
    "Simplicity is the ultimate sophistication. — da Vinci",
    "Talk is cheap. Show me the code. — Torvalds",
    "Premature optimization is the root of all evil. — Knuth",
    "Stay hungry, stay foolish. — Steve Jobs",
};
static const int g_fortune_n = sizeof(g_fortunes)/sizeof(g_fortunes[0]);

const char* fortune_get(int idx) {
    return g_fortunes[((idx % g_fortune_n) + g_fortune_n) % g_fortune_n];
}

static const char* g_jokes[] = {
    "Why do programmers prefer dark mode? Because light attracts bugs.",
    "A SQL query walks into a bar, sees two tables and asks: Mind if I join you?",
    "Why did the developer go broke? Because he used up all his cache.",
    "There are only 10 kinds of people: those who understand binary and those who don't.",
};
static const int g_joke_n = sizeof(g_jokes)/sizeof(g_jokes[0]);

const char* joke_get(int idx) {
    return g_jokes[((idx % g_joke_n) + g_joke_n) % g_joke_n];
}

// -----------------------------------------------------------------------------
//  命令
// -----------------------------------------------------------------------------
static int cmd_cowsay(int argc, const char** argv, TermOutput* out) {
    nefu::String msg;
    for (int i = 1; i < argc; i++) { msg += argv[i]; if (i + 1 < argc) msg += ' '; }
    if (msg.empty()) msg = "Moo!";
    nefu::List<nefu::String> lines;
    word_wrap(msg.c_str(), 40, lines);
    int maxlen = 0;
    for (int i = 0; i < lines.size(); i++)
        if (lines[i].len() > maxlen) maxlen = lines[i].len();
    // 上边框
    nefu::String top = " ";
    for (int i = 0; i < maxlen + 2; i++) top += '-';
    out->pln(top.c_str());
    for (int i = 0; i < lines.size(); i++) {
        nefu::String l = i == 0 ? "/" : (i == lines.size() - 1 ? "\\" : "|");
        l += ' '; l += lines[i];
        int pad = maxlen - lines[i].len();
        for (int p = 0; p < pad; p++) l += ' ';
        l += ' ';
        l += (i == 0 ? "\\" : (i == lines.size() - 1 ? "/" : "|"));
        out->pln(l.c_str());
    }
    nefu::String bot = " ";
    for (int i = 0; i < maxlen + 2; i++) bot += '-';
    out->pln(bot.c_str());
    // 牛
    out->pln("        \\   ^__^");
    out->pln("         \\  (oo)\\_______");
    out->pln("            (__)\\       )\\/\\");
    out->pln("                ||----w |");
    out->pln("                ||     ||");
    return 0;
}

static int cmd_fortune(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    Rng rng((uint32_t)(g_term_now_sec ? g_term_now_sec() : 1));
    out->pln(fortune_get(rng.next()));
    return 0;
}

static int cmd_sl(int argc, const char** argv, TermOutput* out) {
    int frame = 0;
    if (argc > 1) frame = nefu::atoi(argv[1]);
    // 小火车两帧
    if (frame % 2 == 0) {
        out->pln("     ====        ________                ___________ ");
        out->pln(" _D _|  |_______/        \\__I_I_____===__|_________| ");
    } else {
        out->pln("      ====       ________                ___________ ");
        out->pln("  _D _|  |_______/        \\__I_I_____===__|_________| ");
    }
    out->pln("  (_)---|--|                  /\\        |-|   ___--\\ ");
    out->pln("  /|   |===|o|               /  \\       | |__/     \\___");
    return 0;
}

static int cmd_figlet(int argc, const char** argv, TermOutput* out) {
    if (argc < 2) { out->pln("usage: figlet <text>"); return 1; }
    nefu::String msg;
    for (int i = 1; i < argc; i++) { msg += argv[i]; if (i+1<argc) msg += ' '; }
    nefu::List<nefu::String> rows;
    figlet_render(msg.c_str(), rows);
    for (int i = 0; i < rows.size(); i++) out->pln(rows[i].c_str());
    return 0;
}

static int cmd_random(int argc, const char** argv, TermOutput* out) {
    int lo = 0, hi = 100;
    if (argc >= 3) { lo = nefu::atoi(argv[1]); hi = nefu::atoi(argv[2]); }
    Rng rng((uint32_t)(g_term_now_sec ? g_term_now_sec() : 7) ^ 0x9e37);
    out->pfln("%d", rng.range(lo, hi));
    return 0;
}

static int cmd_joke(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    Rng rng((uint32_t)(g_term_now_sec ? g_term_now_sec() : 3));
    out->pln(joke_get(rng.next()));
    return 0;
}

static int cmd_quote(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    out->pln("“The only way to do great work is to love what you do.” — Jobs");
    return 0;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int funcmd_self_test() {
    int fails = 0;
    // 1) 折行: 宽 10, 长句应折成多行且每行不超过宽度
    {
        nefu::List<nefu::String> lines;
        word_wrap("the quick brown fox jumps", 10, lines);
        if (lines.size() < 2) fails++;
        for (int i = 0; i < lines.size(); i++)
            if (lines[i].len() > 10) fails++;
    }
    // 2) figlet 渲染 7 行, 且非空
    {
        nefu::List<nefu::String> rows;
        figlet_render("A", rows);
        if (rows.size() != 7) fails++;
        bool has_pixel = false;
        for (int i = 0; i < rows.size(); i++)
            if (rows[i].find('#') >= 0) has_pixel = true;
        if (!has_pixel) fails++;
    }
    // 3) fortune 循环取界
    {
        for (int i = -2; i < 10; i++) {
            const char* f = fortune_get(i);
            if (!f || !*f) fails++;
        }
    }
    // 4) cowsay 命令产出边框与牛
    {
        const char* av[3] = {"cowsay", "hello", "world"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_cowsay(3, av, &o);
        if (!b.contains("hello")) fails++;
        if (!b.contains("(oo)")) fails++;
    }
    // 5) random 落在区间
    {
        const char* av[3] = {"random", "5", "5"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_random(3, av, &o);
        if (!b.contains("5")) fails++;
    }
    // 6) joke / quote
    {
        BufferTermOutput b; TermOutput o = b.out();
        cmd_joke(0, 0, &o);
        if (b.line_count() < 1) fails++;
        b.clear();
        cmd_quote(0, 0, &o);
        if (!b.contains("great work")) fails++;
    }
    // 7) sl 不崩
    {
        const char* av[2] = {"sl", "1"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_sl(2, av, &o);
        if (b.line_count() < 3) fails++;
    }
    return fails;
}

} // namespace termcmds
} // namespace nefu
