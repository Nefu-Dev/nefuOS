// nefuOS regex library — implementation
// Backtracking regex VM (Thompson-style instruction program).
// Portable: no STL, no exceptions.
#include "regex.h"

namespace nefu {
namespace regex {

namespace {

enum Op {
    OP_CHAR = 0,     // arg a = byte
    OP_ANY,          // any byte except '\n'
    OP_CLASS,        // ranges + negated
    OP_BOL,          // start of text
    OP_EOL,          // end of text (or before '\n')
    OP_SAVE_START,   // arg a = group id: record group start pos
    OP_SAVE_END,     // arg a = group id: record group end pos
    OP_JMP,          // arg a = target pc
    OP_SPLIT,        // arg a = pc A (try first), arg b = pc B (-1 = fail)
    OP_MATCH
};

struct Range { int lo, hi; };

struct Inst {
    Op op;
    int a, b;
    List<Range> ranges;
    bool negated;
    Inst() : op(OP_MATCH), a(0), b(-1), negated(false) {}
};

List<Inst> g_prog;
int g_group_count = 0;
bool g_compiled = false;

int emit(Op op) {
    Inst in;
    in.op = op;
    g_prog.push(in);
    return g_prog.size() - 1;
}

// copy instruction range [src_begin, src_end) to the end, remapping targets
// that point inside the range.
void copy_range(int src_begin, int src_end) {
    int len = src_end - src_begin;
    for (int i = 0; i < len; i++) {
        Inst cp = g_prog[src_begin + i];
        if (cp.op == OP_JMP && cp.a >= src_begin && cp.a < src_end) cp.a += len;
        if (cp.op == OP_SPLIT) {
            if (cp.a >= src_begin && cp.a < src_end) cp.a += len;
            if (cp.b >= src_begin && cp.b < src_end) cp.b += len;
        }
        g_prog.push(cp);
    }
}

// ===================== parser =====================

struct Builder {
    const char* p;
    const char* err;
    bool fail;
    int group_count;

    Builder(const char* s) : p(s), err(0), fail(false), group_count(0) {}

    bool fail_at(const char* m) { if (!fail) { fail = true; err = m; } return false; }
    char peek() { return *p; }
    char next() { char c = *p; if (c) p++; return c; }
    bool is_quant(char c) { return c == '*' || c == '+' || c == '?' || c == '{'; }

    int parse_alt();
    int parse_concat();
    int parse_quant();
    int parse_atom();
    bool parse_class(Inst& out);
};

int Builder::parse_concat() {
    int start = g_prog.size();
    for (;;) {
        char c = peek();
        if (!c || c == '|' || c == ')') break;
        if (is_quant(c)) { fail_at("nothing to repeat"); return -1; }
        if (parse_quant() < 0) return -1;
        if (fail) return -1;
    }
    return start;
}

int Builder::parse_alt() {
    // collect all branches
    List<int> starts;
    int first = parse_concat();
    if (fail) return -1;
    starts.push(first);
    while (peek() == '|') {
        next();
        int s = parse_concat();
        if (fail) return -1;
        starts.push(s);
    }
    int n = starts.size();
    if (n == 1) return starts[0];

    // Patch: every branch except the last gets a Jmp to the common End.
    // End = current program end + (n-1) jumps still to insert.
    int end = g_prog.size() + (n - 1);
    for (int i = n - 2; i >= 0; i--) {
        // insert Jmp(End) at the start of branch i+1 (end of branch i)
        g_prog.insert(starts[i + 1], Inst());
        g_prog[starts[i + 1]].op = OP_JMP;
        g_prog[starts[i + 1]].a = end;
    }
    // after inserting jumps, every branch shifted by (n-1)
    for (int i = 0; i < n; i++) starts[i] += (n - 1);
    // build the split chain at the front: entry = Split(b0, Split(b1, ... bn-1))
    int entry = starts[n - 1];
    for (int i = n - 2; i >= 0; i--) {
        g_prog.insert(0, Inst());
        g_prog[0].op = OP_SPLIT;
        g_prog[0].a = starts[i];
        g_prog[0].b = entry;
        entry = 0;
        // everything shifted by 1: bump all recorded starts and the End
        for (int k = 0; k < n; k++) starts[k]++;
        end++;
    }
    return entry;
}

int Builder::parse_quant() {
    int atom_start = parse_atom();
    if (fail) return -1;
    char c = peek();
    if (c == '*') {
        next();
        // structure: Split(body, exit); body...; Jmp -> split
        int split = atom_start;
        g_prog.insert(split, Inst());
        g_prog[split].op = OP_SPLIT;
        g_prog[split].a = split + 1;      // body (the atom, now shifted +1)
        g_prog[split].b = -1;             // exit, patched below
        int jmp = emit(OP_JMP);
        g_prog[jmp].a = split;            // loop back to the split
        g_prog[split].b = jmp + 1;        // exit = after the jmp
        return split;
    }
    if (c == '+') {
        next();
        // structure: body...; Split(body-start, exit)
        int sp = emit(OP_SPLIT);
        g_prog[sp].a = atom_start;
        g_prog[sp].b = sp + 1;            // exit = after the split
        return atom_start;
    }
    if (c == '?') {
        next();
        // structure: Split(body, exit); body...
        int split = atom_start;
        g_prog.insert(split, Inst());
        g_prog[split].op = OP_SPLIT;
        g_prog[split].a = split + 1;      // body
        g_prog[split].b = -1;             // exit, patched below
        g_prog[split].b = g_prog.size();  // exit = end of body
        return split;
    }
    if (c == '{') {
        next();
        int mn = 0, mx = -1;
        if (peek() >= '0' && peek() <= '9') {
            mn = 0;
            while (peek() >= '0' && peek() <= '9') mn = mn * 10 + (next() - '0');
            if (mn > 64) mn = 64;
        } else { fail_at("bad {n,m}"); return -1; }
        if (peek() == ',') {
            next();
            if (peek() >= '0' && peek() <= '9') {
                mx = 0;
                while (peek() >= '0' && peek() <= '9') mx = mx * 10 + (next() - '0');
                if (mx > 64) mx = 64;
            } else mx = -1;
        } else mx = mn;
        if (peek() != '}') { fail_at("expected '}'"); return -1; }
        next();
        if (mx >= 0 && mx < mn) { fail_at("bad repetition range"); return -1; }

        int body_begin = atom_start;
        int body_end = g_prog.size();
        // mandatory copies (first copy already in place)
        if (mn == 0) {
            // remove the body entirely
            for (int i = body_end - 1; i >= body_begin; i--) g_prog.remove(i);
        } else {
            for (int k = 1; k < mn; k++) copy_range(body_begin, body_end);
        }
        // optional copies
        int opt = (mx < 0) ? 64 : (mx - mn);
        for (int k = 0; k < opt; k++) {
            int sp = emit(OP_SPLIT);
            g_prog[sp].a = sp + 1;        // take this copy (body follows)
            g_prog[sp].b = -1;            // skip, patched after copy
            copy_range(body_begin, body_end);
            g_prog[sp].b = g_prog.size(); // skip to after this copy
        }
        return atom_start;
    }
    return atom_start;
}

bool Builder::parse_class(Inst& out) {
    out.ranges.erase_all();
    out.negated = false;
    if (peek() == '^') { out.negated = true; next(); }
    bool first = true;
    for (;;) {
        char c = peek();
        if (!c) { fail_at("unterminated class"); return false; }
        if (c == ']' && !first) { next(); return true; }
        first = false;
        int lo;
        if (c == '\\') {
            next();
            char e = next();
            if (!e) { fail_at("bad class escape"); return false; }
            switch (e) {
            case 't': lo = '\t'; break;
            case 'n': lo = '\n'; break;
            case 'r': lo = '\r'; break;
            case 'f': lo = '\f'; break;
            case 'd': out.ranges.push(Range{'0', '9'}); continue;
            case 'D':
                out.ranges.push(Range{0, '0' - 1});
                out.ranges.push(Range{'9' + 1, 255});
                continue;
            case 'w':
                out.ranges.push(Range{'a', 'z'});
                out.ranges.push(Range{'A', 'Z'});
                out.ranges.push(Range{'0', '9'});
                out.ranges.push(Range{'_', '_'});
                continue;
            case 'W':
                out.ranges.push(Range{0, '0' - 1});
                out.ranges.push(Range{'9' + 1, 'A' - 1});
                out.ranges.push(Range{'Z' + 1, '_' - 1});
                out.ranges.push(Range{'_' + 1, 'a' - 1});
                out.ranges.push(Range{'z' + 1, 255});
                continue;
            case 's':
                out.ranges.push(Range{' ', ' '});
                out.ranges.push(Range{'\t', '\t'});
                out.ranges.push(Range{'\n', '\n'});
                out.ranges.push(Range{'\r', '\r'});
                out.ranges.push(Range{'\f', '\f'});
                continue;
            case 'S':
                out.ranges.push(Range{0, ' ' - 1});
                out.ranges.push(Range{' ' + 1, '\t' - 1});
                out.ranges.push(Range{'\t' + 1, '\n' - 1});
                out.ranges.push(Range{'\n' + 1, '\r' - 1});
                out.ranges.push(Range{'\r' + 1, '\f' - 1});
                out.ranges.push(Range{'\f' + 1, 255});
                continue;
            default: lo = e; break;
            }
        } else {
            next();
            lo = c;
        }
        if (peek() == '-') {
            const char* save = p;
            next();
            char c2 = peek();
            if (c2 == ']' || c2 == 0 || c2 == '\\') {
                p = save;
                out.ranges.push(Range{lo, lo});
                continue;
            }
            next();
            if (c2 < lo) { fail_at("bad class range"); return false; }
            out.ranges.push(Range{lo, (int)c2});
        } else {
            out.ranges.push(Range{lo, lo});
        }
    }
}

int Builder::parse_atom() {
    char c = peek();
    if (!c) { fail_at("unexpected end"); return -1; }
    if (c == '(') {
        next();
        bool capturing = true;
        if (peek() == '?') {
            next();
            if (peek() == ':') { next(); capturing = false; }
            else { fail_at("unsupported group prefix"); return -1; }
        }
        int gid = 0;
        if (capturing) {
            gid = ++group_count;
            g_group_count = group_count;
            emit(OP_SAVE_START);
            g_prog[g_prog.size() - 1].a = gid;
        }
        int alt_start = parse_alt();
        if (fail) return -1;
        if (peek() != ')') { fail_at("missing ')'"); return -1; }
        next();
        if (capturing) {
            emit(OP_SAVE_END);
            g_prog[g_prog.size() - 1].a = gid;
        }
        return alt_start;
    }
    if (c == '[') {
        next();
        Inst out;
        if (!parse_class(out)) return -1;
        int n = emit(OP_CLASS);
        g_prog[n].ranges = out.ranges;
        g_prog[n].negated = out.negated;
        return n;
    }
    if (c == '.') { next(); return emit(OP_ANY); }
    if (c == '^') { next(); return emit(OP_BOL); }
    if (c == '$') { next(); return emit(OP_EOL); }
    if (c == '\\') {
        next();
        char e = next();
        if (!e) { fail_at("bad escape"); return -1; }
        switch (e) {
        case 't': { int n = emit(OP_CHAR); g_prog[n].a = '\t'; return n; }
        case 'n': { int n = emit(OP_CHAR); g_prog[n].a = '\n'; return n; }
        case 'r': { int n = emit(OP_CHAR); g_prog[n].a = '\r'; return n; }
        case 'f': { int n = emit(OP_CHAR); g_prog[n].a = '\f'; return n; }
        case 'd': {
            int n = emit(OP_CLASS);
            g_prog[n].ranges.push(Range{'0', '9'});
            return n;
        }
        case 'D': {
            int n = emit(OP_CLASS);
            g_prog[n].negated = true;
            g_prog[n].ranges.push(Range{'0', '9'});
            return n;
        }
        case 'w': {
            int n = emit(OP_CLASS);
            g_prog[n].ranges.push(Range{'a', 'z'});
            g_prog[n].ranges.push(Range{'A', 'Z'});
            g_prog[n].ranges.push(Range{'0', '9'});
            g_prog[n].ranges.push(Range{'_', '_'});
            return n;
        }
        case 'W': {
            int n = emit(OP_CLASS);
            g_prog[n].negated = true;
            g_prog[n].ranges.push(Range{'a', 'z'});
            g_prog[n].ranges.push(Range{'A', 'Z'});
            g_prog[n].ranges.push(Range{'0', '9'});
            g_prog[n].ranges.push(Range{'_', '_'});
            return n;
        }
        case 's': {
            int n = emit(OP_CLASS);
            g_prog[n].ranges.push(Range{' ', ' '});
            g_prog[n].ranges.push(Range{'\t', '\t'});
            g_prog[n].ranges.push(Range{'\n', '\n'});
            g_prog[n].ranges.push(Range{'\r', '\r'});
            g_prog[n].ranges.push(Range{'\f', '\f'});
            return n;
        }
        case 'S': {
            int n = emit(OP_CLASS);
            g_prog[n].negated = true;
            g_prog[n].ranges.push(Range{' ', ' '});
            g_prog[n].ranges.push(Range{'\t', '\t'});
            g_prog[n].ranges.push(Range{'\n', '\n'});
            g_prog[n].ranges.push(Range{'\r', '\r'});
            g_prog[n].ranges.push(Range{'\f', '\f'});
            return n;
        }
        case 'x': {
            int v = 0;
            for (int k = 0; k < 2; k++) {
                char h = peek();
                int d;
                if (h >= '0' && h <= '9') d = h - '0';
                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                else break;
                v = v * 16 + d;
                next();
            }
            int n = emit(OP_CHAR);
            g_prog[n].a = v & 0xFF;
            return n;
        }
        default: {
            int n = emit(OP_CHAR);
            g_prog[n].a = (int)(unsigned char)e;
            return n;
        }
        }
    }
    next();
    int n = emit(OP_CHAR);
    g_prog[n].a = (int)(unsigned char)c;
    return n;
}

// ===================== VM execution (backtracking) =====================

struct VM {
    const char* text;
    int len;
    Match* m;
    int depth;

    bool run(int pc, int pos) {
        if (++depth > 8000) { depth--; return false; }
        for (;;) {
            if (pc < 0 || pc >= g_prog.size()) { depth--; return false; }
            const Inst& in = g_prog[pc];
            switch (in.op) {
            case OP_CHAR:
                if (pos < len && (unsigned char)text[pos] == (unsigned char)in.a) { pos++; pc++; break; }
                depth--; return false;
            case OP_ANY:
                if (pos < len && text[pos] != '\n') { pos++; pc++; break; }
                depth--; return false;
            case OP_CLASS: {
                if (pos >= len) { depth--; return false; }
                unsigned char c = (unsigned char)text[pos];
                bool ok = false;
                for (int i = 0; i < in.ranges.size(); i++) {
                    if (c >= (unsigned char)in.ranges[i].lo && c <= (unsigned char)in.ranges[i].hi) { ok = true; break; }
                }
                if (in.negated ? !ok : ok) { pos++; pc++; break; }
                depth--; return false;
            }
            case OP_BOL:
                if (pos == 0) { pc++; break; }
                depth--; return false;
            case OP_EOL:
                if (pos == len || text[pos] == '\n') { pc++; break; }
                depth--; return false;
            case OP_SAVE_START: {
                int gid = in.a;
                if (gid >= 0 && gid <= 9) {
                    int save0 = m->groups[gid][0];
                    int save1 = m->groups[gid][1];
                    m->groups[gid][0] = pos;
                    m->groups[gid][1] = pos;
                    if (run(pc + 1, pos)) { depth--; return true; }
                    m->groups[gid][0] = save0;
                    m->groups[gid][1] = save1;
                }
                depth--; return false;
            }
            case OP_SAVE_END: {
                int gid = in.a;
                if (gid >= 0 && gid <= 9) {
                    int save1 = m->groups[gid][1];
                    m->groups[gid][1] = pos;
                    if (run(pc + 1, pos)) { depth--; return true; }
                    m->groups[gid][1] = save1;
                }
                depth--; return false;
            }
            case OP_JMP:
                pc = in.a;
                break;
            case OP_SPLIT:
                if (run(in.a, pos)) { depth--; return true; }
                if (in.b >= 0 && run(in.b, pos)) { depth--; return true; }
                depth--; return false;
            case OP_MATCH:
                if (m) {
                    int g0s = m->groups[0][0];
                    m->start = (g0s >= 0) ? g0s : 0;
                    m->len = pos - m->start;
                    m->groups[0][1] = pos;
                }
                depth--; return true;
            }
        }
    }
};

} // namespace

// ===================== public API =====================

bool compile(const char* pattern, const char** err) {
    g_prog.erase_all();
    g_group_count = 0;
    g_compiled = false;
    if (!pattern) { if (err) *err = "null pattern"; return false; }
    Builder b(pattern);
    int alt_start = b.parse_alt();
    if (b.fail || alt_start < 0) {
        if (err) *err = b.err ? b.err : "compile error";
        return false;
    }
    if (b.peek() != 0) {
        if (err) *err = "trailing pattern characters";
        return false;
    }
    // group 0 capture: save start at program head, save end at the tail
    g_prog.insert(0, Inst());
    g_prog[0].op = OP_SAVE_START;
    g_prog[0].a = 0;
    // everything shifted by 1: remap all targets
    for (int i = 0; i < g_prog.size(); i++) {
        Inst& in = g_prog[i];
        if (in.op == OP_JMP && in.a >= 0) in.a++;
        if (in.op == OP_SPLIT) {
            if (in.a >= 0) in.a++;
            if (in.b >= 0) in.b++;
        }
    }
    emit(OP_SAVE_END);
    g_prog[g_prog.size() - 1].a = 0;
    emit(OP_MATCH);
    g_compiled = true;
    return true;
}

void reset_state() {}

bool search(const char* text, Match& m) {
    m.reset();
    if (!g_compiled || !text) return false;
    int len = (int)strlen(text);
    for (int start = 0; start <= len; start++) {
        VM vm;
        vm.text = text;
        vm.len = len;
        vm.m = &m;
        vm.depth = 0;
        m.groups[0][0] = start;
        m.groups[0][1] = start;
        if (vm.run(0, start)) return true;
    }
    m.reset();
    return false;
}

bool full_match(const char* text) {
    if (!text) return false;
    Match m;
    if (!search(text, m)) return false;
    return m.start == 0 && m.len == (int)strlen(text);
}

bool contains(const char* text) {
    Match m;
    return search(text, m);
}

bool match_anywhere(const char* pattern, const char* text, Match* m) {
    if (!compile(pattern)) return false;
    Match tmp;
    if (search(text, tmp)) {
        if (m) *m = tmp;
        return true;
    }
    return false;
}

bool match_full(const char* pattern, const char* text) {
    if (!compile(pattern)) return false;
    return full_match(text);
}

} // namespace regex
} // namespace nefu
