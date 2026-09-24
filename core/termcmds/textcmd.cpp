// =============================================================================
//  textcmd.cpp —— 文本处理命令实现
// =============================================================================
#include "textcmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  纯算法实现
// -----------------------------------------------------------------------------
bool grep_line_match(const char* line, const char* pattern, bool icase) {
    if (!line || !pattern) return false;
    if (icase) {
        // 大小写不敏感的子串查找
        int L = (int)nefu::strlen(line);
        int P = (int)nefu::strlen(pattern);
        if (P == 0) return true;
        for (int i = 0; i + P <= L; i++) {
            bool ok = true;
            for (int j = 0; j < P; j++) {
                if (to_lower(line[i + j]) != to_lower(pattern[j])) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    }
    return nefu::strstr(line, pattern) != 0;
}

void wc_count(const char* text, int* out_lines, int* out_words, int* out_chars) {
    int lines = 0, words = 0, chars = 0;
    bool in_word = false;
    if (text) {
        for (const char* p = text; *p; p++) {
            chars++;
            if (*p == '\n') lines++;
            bool sp = is_sp(*p) || *p == '\n' || *p == '\r';
            if (sp) { in_word = false; }
            else if (!in_word) { words++; in_word = true; }
        }
    }
    if (out_lines) *out_lines = lines;
    if (out_words) *out_words = words;
    if (out_chars) *out_chars = chars;
}

// 字符串转 int(支持前导负号)
static int str_to_int(const char* s) {
    int v = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (is_digit(*s)) { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}

// 比较两行: numeric 时按行首整数; 否则字典序。返回 <0 / 0 / >0。
static int compare_lines(const nefu::String& a, const nefu::String& b, bool numeric) {
    if (numeric) return str_to_int(a.c_str()) - str_to_int(b.c_str());
    return nefu::strcmp(a.c_str(), b.c_str());
}

void sort_lines(nefu::List<nefu::String>& lines, bool numeric, bool reverse) {
    // 插入排序(对测试规模足够稳定,无 STL)
    int n = lines.size();
    for (int i = 1; i < n; i++) {
        nefu::String key = lines[i];
        int j = i - 1;
        while (j >= 0) {
            int c = compare_lines(lines[j], key, numeric);
            if (reverse) c = -c;
            if (c <= 0) break;
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = key;
    }
}

nefu::String sed_substitute(const char* line, const char* old, const char* nw, bool global) {
    nefu::String out;
    if (!line) return out;
    if (!old || !*old) { out = line; return out; }
    const char* p = line;
    while (*p) {
        // 尝试匹配 old
        const char* q = old;
        const char* r = p;
        while (*q && *r && *q == *r) { q++; r++; }
        if (!*q) {
            out += nw ? nw : "";
            p = r;
            if (!global) { while (*p) out += *p++; }
        } else {
            out += *p++;
        }
    }
    return out;
}

nefu::String awk_field(const char* line, char sep, int field) {
    nefu::String out;
    if (!line || field < 1) return out;
    int cur = 0;
    const char* p = line;
    if (sep == 0) {
        // 空白模式: 连续空白视为一个分隔
        while (*p && is_sp(*p)) p++;
        while (*p) {
            cur++;
            const char* start = p;
            while (*p && !is_sp(*p)) p++;
            if (cur == field) { out = nefu::String(start, (int)(p - start)); break; }
            while (*p && is_sp(*p)) p++;
        }
    } else {
        while (*p) {
            cur++;
            const char* start = p;
            while (*p && *p != sep) p++;
            if (cur == field) { out = nefu::String(start, (int)(p - start)); break; }
            if (*p == sep) p++;
        }
    }
    return out;
}

bool text_load_lines(int argc, const char** argv, int file_arg_index,
                     nefu::List<nefu::String>& out, TermOutput* err) {
    if (file_arg_index < argc && argv[file_arg_index]) {
        nefu::String content;
        if (!vfs_read_file(argv[file_arg_index], content)) {
            if (err) err->pfln("%s: %s: no such file", argv[0], argv[file_arg_index]);
            return false;
        }
        split_lines(content.c_str(), out);
    } else if (g_term_stdin) {
        split_lines(g_term_stdin->c_str(), out);
    } else {
        if (err) err->pln("usage: need file argument or stdin");
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
//  命令实现
// -----------------------------------------------------------------------------
static int cmd_grep(int argc, const char** argv, TermOutput* out) {
    bool icase = false, inv = false, count = false;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        for (const char* f = argv[i] + 1; *f; f++) {
            if (*f == 'i') icase = true;
            else if (*f == 'v') inv = true;
            else if (*f == 'c') count = true;
        }
        i++;
    }
    if (i >= argc) { out->pln("usage: grep [-ivc] <pattern> [file]"); return 1; }
    const char* pat = argv[i++];
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    int hits = 0;
    for (int k = 0; k < lines.size(); k++) {
        bool m = grep_line_match(lines[k].c_str(), pat, icase);
        if (inv) m = !m;
        if (m) {
            hits++;
            if (!count) out->pln(lines[k].c_str());
        }
    }
    if (count) out->pfln("%d", hits);
    return 0;
}

// sed s/old/new/[g]
static int cmd_sed(int argc, const char** argv, TermOutput* out) {
    if (argc < 2) { out->pln("usage: sed s/old/new/[g] [file]"); return 1; }
    const char* spec = argv[1];
    if (spec[0] != 's' || spec[1] != '/') { out->pln("sed: only s/old/new/[g] supported"); return 1; }
    char delim = '/';
    const char* p = spec + 2;
    char old[128], nw[128];
    int n = 0;
    while (*p && *p != delim && n < 127) old[n++] = *p++;
    old[n] = 0;
    if (*p != delim) { out->pln("sed: bad pattern"); return 1; }
    p++;
    n = 0;
    while (*p && *p != delim && n < 127) nw[n++] = *p++;
    nw[n] = 0;
    bool global = false;
    if (*p == delim) { p++; if (*p == 'g') global = true; }

    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, 2, lines, out)) return 1;
    for (int k = 0; k < lines.size(); k++) {
        nefu::String r = sed_substitute(lines[k].c_str(), old, nw, global);
        out->pln(r.c_str());
    }
    return 0;
}

// awk [-F sep] <field> [file]
static int cmd_awk(int argc, const char** argv, TermOutput* out) {
    char sep = 0; // 0 = whitespace
    int i = 1;
    if (i < argc && nefu::strcmp(argv[i], "-F") == 0) {
        i++;
        if (i < argc && argv[i][0]) sep = argv[i][0];
        i++;
    }
    if (i >= argc) { out->pln("usage: awk [-F sep] <fieldnum> [file]"); return 1; }
    int field = nefu::atoi(argv[i]);
    i++;
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    for (int k = 0; k < lines.size(); k++) {
        nefu::String f = awk_field(lines[k].c_str(), sep, field);
        out->pln(f.c_str());
    }
    return 0;
}

static int cmd_sort(int argc, const char** argv, TermOutput* out) {
    bool numeric = false, rev = false;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        for (const char* f = argv[i] + 1; *f; f++) {
            if (*f == 'n') numeric = true;
            if (*f == 'r') rev = true;
        }
        i++;
    }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    sort_lines(lines, numeric, rev);
    for (int k = 0; k < lines.size(); k++) out->pln(lines[k].c_str());
    return 0;
}

static int cmd_uniq(int argc, const char** argv, TermOutput* out) {
    bool show_count = false;
    int i = 1;
    if (i < argc && nefu::strcmp(argv[i], "-c") == 0) { show_count = true; i++; }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    int k = 0;
    while (k < lines.size()) {
        int run = 1;
        while (k + run < lines.size() && lines[k + run] == lines[k]) run++;
        if (show_count) out->pfln("%d %s", run, lines[k].c_str());
        else out->pln(lines[k].c_str());
        k += run;
    }
    return 0;
}

static int cmd_wc(int argc, const char** argv, TermOutput* out) {
    bool l = false, w = false, c = false;
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        for (const char* f = argv[i] + 1; *f; f++) {
            if (*f == 'l') l = true;
            if (*f == 'w') w = true;
            if (*f == 'c') c = true;
        }
        i++;
    }
    if (!l && !w && !c) { l = w = c = true; }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    int L = lines.size();
    int W = 0, C = 0;
    for (int k = 0; k < lines.size(); k++) {
        bool in_w = false;
        for (int j = 0; j < lines[k].len(); j++) {
            C++;
            bool sp = is_sp(lines[k][j]);
            if (sp) in_w = false;
            else if (!in_w) { W++; in_w = true; }
        }
    }
    if (l) out->pf("%d ", L);
    if (w) out->pf("%d ", W);
    if (c) out->pf("%d ", C);
    out->pln();
    return 0;
}

static int cmd_head(int argc, const char** argv, TermOutput* out) {
    int n = 10;
    int i = 1;
    if (i < argc && nefu::strcmp(argv[i], "-n") == 0) { i++; n = nefu::atoi(argv[i]); i++; }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    int lim = n < lines.size() ? n : lines.size();
    for (int k = 0; k < lim; k++) out->pln(lines[k].c_str());
    return 0;
}

static int cmd_tail(int argc, const char** argv, TermOutput* out) {
    int n = 10;
    int i = 1;
    if (i < argc && nefu::strcmp(argv[i], "-n") == 0) { i++; n = nefu::atoi(argv[i]); i++; }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    int start = lines.size() - n; if (start < 0) start = 0;
    for (int k = start; k < lines.size(); k++) out->pln(lines[k].c_str());
    return 0;
}

static int cmd_rev(int argc, const char** argv, TermOutput* out) {
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, 1, lines, out)) return 1;
    for (int k = 0; k < lines.size(); k++) {
        nefu::String s = lines[k];
        nefu::String r;
        for (int j = s.len() - 1; j >= 0; j--) r += s[j];
        out->pln(r.c_str());
    }
    return 0;
}

static int cmd_cat(int argc, const char** argv, TermOutput* out) {
    bool number = false;
    int i = 1;
    if (i < argc && nefu::strcmp(argv[i], "-n") == 0) { number = true; i++; }
    if (i >= argc) { out->pln("usage: cat [-n] <file>"); return 1; }
    nefu::List<nefu::String> lines;
    if (!text_load_lines(argc, argv, i, lines, out)) return 1;
    for (int k = 0; k < lines.size(); k++) {
        if (number) out->pfln("%6d  %s", k + 1, lines[k].c_str());
        else out->pln(lines[k].c_str());
    }
    return 0;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int textcmd_self_test() {
    int fails = 0;
    vfs_init();
    while (vfs_root()->children.size()) vfs_remove(vfs_root()->children[0]->name);

    // 准备一个测试文件
    const char* tcontent =
        "apple banana\n"
        "cherry date\n"
        "Apple elderberry\n"
        "fig grape\n";
    vfs_write_file("/t.txt", tcontent, (int)nefu::strlen(tcontent));
    {
        int L, W, C;
        wc_count("hello world\nfoo bar baz\n", &L, &W, &C);
        if (L != 2) fails++;
        if (W != 5) fails++;
        if (C != 24) fails++;
    }
    // grep 大小写不敏感
    if (!grep_line_match("Apple pie", "apple", true)) fails++;
    if (grep_line_match("Apple pie", "xyz", true)) fails++;
    if (!grep_line_match("hello", "ell", false)) fails++;

    // grep -i 命令
    {
        const char* av[4] = {"grep", "-i", "apple", "/t.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_grep(4, av, &o);
        if (!b.contains("apple banana")) fails++;
        if (!b.contains("Apple elderberry")) fails++;
    }
    // grep -ic 命令(命中两行)
    {
        const char* av[4] = {"grep", "-ic", "apple", "/t.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_grep(4, av, &o);
        if (!b.contains("2")) fails++;
    }
    // sed 替换
    {
        nefu::String r = sed_substitute("aaa bbb aaa", "aaa", "X", true);
        if (r != "X bbb X") fails++;
        nefu::String r2 = sed_substitute("aaa bbb aaa", "aaa", "X", false);
        if (r2 != "X bbb aaa") fails++;
    }
    // sed 命令
    {
        const char* scontent = "foo foo bar\n";
        vfs_write_file("/s.txt", scontent, (int)nefu::strlen(scontent));
        const char* av[3] = {"sed", "s/foo/qux/g", "/s.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_sed(3, av, &o);
        if (!b.contains("qux qux bar")) fails++;
    }
    // awk 字段
    {
        nefu::String f = awk_field("one two three", 0, 2);
        if (f != "two") fails++;
        nefu::String f2 = awk_field("a,b,c,d", ',', 3);
        if (f2 != "c") fails++;
    }
    // sort 字典序
    {
        nefu::List<nefu::String> L;
        L.push(nefu::String("banana"));
        L.push(nefu::String("apple"));
        L.push(nefu::String("cherry"));
        sort_lines(L, false, false);
        if (!(L[0] == "apple" && L[1] == "banana" && L[2] == "cherry")) fails++;
    }
    // sort 数值
    {
        nefu::List<nefu::String> L;
        L.push(nefu::String("10"));
        L.push(nefu::String("2"));
        L.push(nefu::String("21"));
        sort_lines(L, true, false);
        if (!(L[0] == "2" && L[1] == "10" && L[2] == "21")) fails++;
    }
    // wc 命令
    {
        const char* av[2] = {"wc", "/t.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_wc(2, av, &o);
        if (!b.contains("4")) fails++; // 4 lines
    }
    // uniq
    {
        const char* ucontent = "a\na\nb\nb\nb\nc\n";
        vfs_write_file("/u.txt", ucontent, (int)nefu::strlen(ucontent));
        const char* av[3] = {"uniq", "-c", "/u.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_uniq(3, av, &o);
        if (!b.contains("2 a")) fails++;
        if (!b.contains("3 b")) fails++;
    }
    // head / tail
    {
        const char* ncontent = "1\n2\n3\n4\n5\n";
        vfs_write_file("/n.txt", ncontent, (int)nefu::strlen(ncontent));
        const char* av2[4] = {"head", "-n", "2", "/n.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_head(4, av2, &o);
        if (b.line_count() != 2) fails++;
        if (!b.contains("1") || !b.contains("2")) fails++;
        const char* av3[4] = {"tail", "-n", "2", "/n.txt"};
        BufferTermOutput b2; TermOutput o2 = b2.out();
        cmd_tail(4, av3, &o2);
        if (!b2.contains("4") || !b2.contains("5")) fails++;
    }
    // rev 逻辑
    {
        nefu::String s("abc");
        nefu::String rv;
        for (int j = s.len() - 1; j >= 0; j--) rv += s[j];
        if (rv != "cba") fails++;
    }
    // cat -n
    {
        const char* av[3] = {"cat", "-n", "/t.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_cat(3, av, &o);
        if (!b.contains("1")) fails++;
        if (!b.contains("4")) fails++;
    }

    vfs_shutdown();
    return fails;
}

} // namespace termcmds
} // namespace nefu
