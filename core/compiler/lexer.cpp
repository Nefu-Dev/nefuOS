// ============================================================================
// nefu::compiler —— 词法分析器实现（lexer.cpp）
// ============================================================================
#include "lexer.h"

// 比较两个 n 字节字符串是否相等（本地小工具，避免依赖运行时）
static inline bool streqn(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

namespace nefu {
namespace compiler {

// ---- 可读名字表 ----
struct TkName { int kind; const char* name; };
static const TkName g_tk_names[] = {
    { TK_EOF, "EOF" }, { TK_ERR, "ERR" },
    { TK_IDENT, "ident" }, { TK_INT, "int" }, { TK_FLOAT, "float" },
    { TK_CHAR, "char" }, { TK_STR, "string" },
    { KW_VOID, "void" }, { KW_CHAR, "char" }, { KW_SHORT, "short" },
    { KW_INT, "int" }, { KW_LONG, "long" }, { KW_FLOAT, "float" },
    { KW_DOUBLE, "double" }, { KW_CONST, "const" }, { KW_STATIC, "static" },
    { KW_EXTERN, "extern" }, { KW_SIZEOF, "sizeof" },
    { KW_IF, "if" }, { KW_ELSE, "else" }, { KW_WHILE, "while" },
    { KW_DO, "do" }, { KW_FOR, "for" }, { KW_RETURN, "return" },
    { KW_BREAK, "break" }, { KW_CONTINUE, "continue" },
    { KW_STRUCT, "struct" }, { KW_TYPEDEF, "typedef" },
    { PUN_LSH, "<<" }, { PUN_RSH, ">>" }, { PUN_INC, "++" }, { PUN_DEC, "--" },
    { PUN_ARROW, "->" }, { PUN_LAND, "&&" }, { PUN_LOR, "||" },
    { PUN_LE, "<=" }, { PUN_GE, ">=" }, { PUN_EQ, "==" }, { PUN_NE, "!=" },
    { PUN_ADD_ASSIGN, "+=" }, { PUN_SUB_ASSIGN, "-=" },
    { PUN_MUL_ASSIGN, "*=" }, { PUN_DIV_ASSIGN, "/=" }, { PUN_MOD_ASSIGN, "%=" },
    { PUN_AND_ASSIGN, "&=" }, { PUN_OR_ASSIGN, "|=" }, { PUN_XOR_ASSIGN, "^=" },
    { PUN_LSH_ASSIGN, "<<=" }, { PUN_RSH_ASSIGN, ">>=" },
    { PUN_PLUS, "+" }, { PUN_MINUS, "-" }, { PUN_STAR, "*" },
    { PUN_SLASH, "/" }, { PUN_PERCENT, "%" }, { PUN_AMP, "&" },
    { PUN_PIPE, "|" }, { PUN_CARET, "^" }, { PUN_TILDE, "~" },
    { PUN_BANG, "!" }, { PUN_ASSIGN, "=" }, { PUN_LT, "<" }, { PUN_GT, ">" },
    { PUN_QUEST, "?" }, { PUN_COLON, ":" }, { PUN_SEMI, ";" },
    { PUN_COMMA, "," }, { PUN_DOT, "." },
    { PUN_LPAREN, "(" }, { PUN_RPAREN, ")" },
    { PUN_LBRACE, "{" }, { PUN_RBRACE, "}" },
    { PUN_LBRACKET, "[" }, { PUN_RBRACKET, "]" }
};
static const int g_tk_name_count = (int)(sizeof(g_tk_names) / sizeof(g_tk_names[0]));

const char* tk_name(int kind) {
    for (int i = 0; i < g_tk_name_count; i++)
        if (g_tk_names[i].kind == kind) return g_tk_names[i].name;
    return "?";
}

Lexer::Lexer() : src(0), pos(0), line(1), col(1), error(false) {
    eof_tok.kind = TK_EOF; eof_tok.line = 1; eof_tok.col = 1;
    eof_tok.start = ""; eof_tok.len = 0; eof_tok.ival = 0; eof_tok.fval = 0;
    eof_tok.lit = 0; eof_tok.litlen = 0; eof_tok.litoff = 0;
}
Lexer::Lexer(const char* source) : Lexer() { reset(source); }

void Lexer::reset(const char* source) {
    src = source ? source : "";
    pos = 0; line = 1; col = 1; error = false;
    errmsg.clear(); tokens.clear(); litbuf.clear();
}

char Lexer::cur() const { return src ? src[pos] : 0; }
char Lexer::peekch(int off) const {
    if (!src) return 0;
    int p = pos + off;
    return src[p] ? src[p] : 0;
}
char Lexer::advance() {
    char c = cur();
    if (c == 0) return 0;
    pos++;
    if (c == '\n') { line++; col = 1; } else { col++; }
    return c;
}

bool Lexer::is_id_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool Lexer::is_id_char(char c) {
    return is_id_start(c) || (c >= '0' && c <= '9');
}

void Lexer::emit(const Token& t) { tokens.push(t); }

// ---- 跳过空白与注释 ----
void Lexer::skip_trivia() {
    for (;;) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && peekch(1) == '/') {
            while (cur() != 0 && cur() != '\n') advance();
            continue;
        }
        if (c == '/' && peekch(1) == '*') {
            advance(); advance();
            while (cur() != 0 && !(cur() == '*' && peekch(1) == '/')) advance();
            if (cur() == 0) { error = true; errmsg = "未闭合的块注释"; return; }
            advance(); advance();
            continue;
        }
        break;
    }
}

// ---- 标识符 / 关键字 ----
Token Lexer::read_ident() {
    int start_pos = pos;
    int start_col = col;
    while (is_id_char(cur())) advance();
    int n = pos - start_pos;
    Token t;
    t.kind = TK_IDENT;
    t.line = line; t.col = start_col;
    t.start = src + start_pos; t.len = n;
    t.ival = 0; t.fval = 0; t.lit = 0; t.litlen = 0; t.litoff = 0;
    int kw = keyword_kind(t.start, n);
    if (kw >= 0) t.kind = kw;
    return t;
}

int Lexer::keyword_kind(const char* s, int n) const {
    static const struct { const char* w; int kind; } kws[] = {
        {"void", KW_VOID}, {"char", KW_CHAR}, {"short", KW_SHORT},
        {"int", KW_INT}, {"long", KW_LONG}, {"float", KW_FLOAT},
        {"double", KW_DOUBLE}, {"const", KW_CONST}, {"static", KW_STATIC},
        {"extern", KW_EXTERN}, {"sizeof", KW_SIZEOF},
        {"if", KW_IF}, {"else", KW_ELSE}, {"while", KW_WHILE},
        {"do", KW_DO}, {"for", KW_FOR}, {"return", KW_RETURN},
        {"break", KW_BREAK}, {"continue", KW_CONTINUE},
        {"struct", KW_STRUCT}, {"typedef", KW_TYPEDEF}
    };
    for (int i = 0; i < (int)(sizeof(kws)/sizeof(kws[0])); i++) {
        const char* w = kws[i].w;
        int len = 0; while (w[len]) len++;
        if (len != n) continue;
        if (streqn(s, w, n)) return kws[i].kind;
    }
    return -1;
}

// ---- 数字（十进制 / 十六进制 / 浮点）----
Token Lexer::read_number() {
    int start_pos = pos;
    int start_col = col;
    bool is_float = false;
    int64_t val = 0;
    double fval = 0.0;
    double frac = 0.1;

    if (cur() == '0' && (peekch(1) == 'x' || peekch(1) == 'X')) {
        advance(); advance();
        for (;;) {
            char c = cur();
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            if (d < 0) break;
            val = val * 16 + d;
            advance();
        }
    } else {
        while (cur() >= '0' && cur() <= '9') {
            val = val * 10 + (cur() - '0');
            advance();
        }
        if (cur() == '.' && peekch(1) >= '0' && peekch(1) <= '9') {
            is_float = true;
            advance();
            fval = (double)val;
            while (cur() >= '0' && cur() <= '9') {
                fval += (cur() - '0') * frac;
                frac *= 0.1;
                advance();
            }
        }
        if ((cur() == 'e' || cur() == 'E') &&
            ((peekch(1) >= '0' && peekch(1) <= '9') ||
             ((peekch(1) == '+' || peekch(1) == '-') && peekch(2) >= '0' && peekch(2) <= '9'))) {
            is_float = true;
            advance();
            int esign = 1;
            if (cur() == '+') advance();
            else if (cur() == '-') { esign = -1; advance(); }
            int eval = 0;
            while (cur() >= '0' && cur() <= '9') { eval = eval * 10 + (cur() - '0'); advance(); }
            double scale = 1.0;
            for (int i = 0; i < eval; i++) scale *= 10.0;
            if (esign < 0) fval /= scale; else fval *= scale;
        }
        if (!is_float) fval = (double)val;
    }

    int n = pos - start_pos;
    Token t;
    t.kind = is_float ? TK_FLOAT : TK_INT;
    t.line = line; t.col = start_col;
    t.start = src + start_pos; t.len = n;
    t.ival = is_float ? 0 : val;
    t.fval = fval;
    t.lit = 0; t.litlen = 0; t.litoff = 0;
    return t;
}

// ---- 字符字面量 ----
Token Lexer::read_char() {
    int start_col = col;
    advance();
    int code = 0;
    if (cur() == '\\') {
        advance();
        char e = cur();
        switch (e) {
            case 'n': code = '\n'; break;
            case 't': code = '\t'; break;
            case 'r': code = '\r'; break;
            case '0': code = 0; break;
            case '\\': code = '\\'; break;
            case '\'': code = '\''; break;
            case '"':  code = '"';  break;
            default: code = (unsigned char)e; break;
        }
        advance();
    } else {
        code = (unsigned char)cur();
        advance();
    }
    if (cur() == '\'') advance();
    Token t;
    t.kind = TK_CHAR;
    t.line = line; t.col = start_col;
    t.start = src + pos; t.len = 1;
    t.ival = code; t.fval = 0;
    t.lit = 0; t.litlen = 0; t.litoff = 0;
    return t;
}

// ---- 字符串字面量 ----
Token Lexer::read_string() {
    int start_col = col;
    advance();
    int begin = litbuf.len();
    while (cur() != 0 && cur() != '"') {
        char c = cur();
        if (c == '\\') {
            advance();
            char e = cur();
            switch (e) {
                case 'n': litbuf += '\n'; break;
                case 't': litbuf += '\t'; break;
                case 'r': litbuf += '\r'; break;
                case '0': litbuf += '\0'; break;
                case '\\': litbuf += '\\'; break;
                case '\'': litbuf += '\''; break;
                case '"':  litbuf += '"';  break;
                default: litbuf += e; break;
            }
            advance();
        } else {
            litbuf += c;
            advance();
        }
    }
    if (cur() == '"') advance();
    Token t;
    t.kind = TK_STR;
    t.line = line; t.col = start_col;
    t.start = ""; t.len = 0;
    t.ival = 0; t.fval = 0;
    t.lit = 0;
    t.litoff = begin;
    t.litlen = litbuf.len() - begin;
    return t;
}

// ---- 复合 / 单字符运算符 ----
static int match_punct(Lexer* l) {
    char c = l->cur();
    char c2 = l->peekch(1);
    char c3 = l->peekch(2);
    if (c == '<' && c2 == '<' && c3 == '=') { l->advance(); l->advance(); l->advance(); return PUN_LSH_ASSIGN; }
    if (c == '>' && c2 == '>' && c3 == '=') { l->advance(); l->advance(); l->advance(); return PUN_RSH_ASSIGN; }
    if (c == '<' && c2 == '<') { l->advance(); l->advance(); return PUN_LSH; }
    if (c == '>' && c2 == '>') { l->advance(); l->advance(); return PUN_RSH; }
    if (c == '+' && c2 == '+') { l->advance(); l->advance(); return PUN_INC; }
    if (c == '-' && c2 == '-') { l->advance(); l->advance(); return PUN_DEC; }
    if (c == '-' && c2 == '>') { l->advance(); l->advance(); return PUN_ARROW; }
    if (c == '&' && c2 == '&') { l->advance(); l->advance(); return PUN_LAND; }
    if (c == '|' && c2 == '|') { l->advance(); l->advance(); return PUN_LOR; }
    if (c == '<' && c2 == '=') { l->advance(); l->advance(); return PUN_LE; }
    if (c == '>' && c2 == '=') { l->advance(); l->advance(); return PUN_GE; }
    if (c == '=' && c2 == '=') { l->advance(); l->advance(); return PUN_EQ; }
    if (c == '!' && c2 == '=') { l->advance(); l->advance(); return PUN_NE; }
    if (c == '+' && c2 == '=') { l->advance(); l->advance(); return PUN_ADD_ASSIGN; }
    if (c == '-' && c2 == '=') { l->advance(); l->advance(); return PUN_SUB_ASSIGN; }
    if (c == '*' && c2 == '=') { l->advance(); l->advance(); return PUN_MUL_ASSIGN; }
    if (c == '/' && c2 == '=') { l->advance(); l->advance(); return PUN_DIV_ASSIGN; }
    if (c == '%' && c2 == '=') { l->advance(); l->advance(); return PUN_MOD_ASSIGN; }
    if (c == '&' && c2 == '=') { l->advance(); l->advance(); return PUN_AND_ASSIGN; }
    if (c == '|' && c2 == '=') { l->advance(); l->advance(); return PUN_OR_ASSIGN; }
    if (c == '^' && c2 == '=') { l->advance(); l->advance(); return PUN_XOR_ASSIGN; }
    switch (c) {
        case '+': l->advance(); return PUN_PLUS;
        case '-': l->advance(); return PUN_MINUS;
        case '*': l->advance(); return PUN_STAR;
        case '/': l->advance(); return PUN_SLASH;
        case '%': l->advance(); return PUN_PERCENT;
        case '&': l->advance(); return PUN_AMP;
        case '|': l->advance(); return PUN_PIPE;
        case '^': l->advance(); return PUN_CARET;
        case '~': l->advance(); return PUN_TILDE;
        case '!': l->advance(); return PUN_BANG;
        case '=': l->advance(); return PUN_ASSIGN;
        case '<': l->advance(); return PUN_LT;
        case '>': l->advance(); return PUN_GT;
        case '?': l->advance(); return PUN_QUEST;
        case ':': l->advance(); return PUN_COLON;
        case ';': l->advance(); return PUN_SEMI;
        case ',': l->advance(); return PUN_COMMA;
        case '.': l->advance(); return PUN_DOT;
        case '(': l->advance(); return PUN_LPAREN;
        case ')': l->advance(); return PUN_RPAREN;
        case '{': l->advance(); return PUN_LBRACE;
        case '}': l->advance(); return PUN_RBRACE;
        case '[': l->advance(); return PUN_LBRACKET;
        case ']': l->advance(); return PUN_RBRACKET;
    }
    return -1;
}

// ---- 主扫描循环 ----
bool Lexer::tokenize() {
    skip_trivia();
    while (cur() != 0) {
        int sline = line, scol = col;
        char c = cur();
        Token t;
        t.kind = TK_EOF; t.line = sline; t.col = scol;
        t.ival = 0; t.fval = 0; t.lit = 0; t.litlen = 0; t.litoff = 0;

        if (is_id_start(c)) {
            t = read_ident();
        } else if ((c >= '0' && c <= '9') || (c == '.' && peekch(1) >= '0' && peekch(1) <= '9')) {
            t = read_number();
        } else if (c == '\'') {
            t = read_char();
        } else if (c == '"') {
            t = read_string();
        } else {
            int pk = match_punct(this);
            if (pk < 0) {
                error = true;
                errmsg = "非法字符";
                t.kind = TK_ERR; t.start = src + pos; t.len = 1;
                emit(t);
                advance();
                return false;
            }
            t.kind = pk; t.start = src + pos; t.len = 1;
        }
        emit(t);
        skip_trivia();
    }
    Token eof;
    eof.kind = TK_EOF; eof.line = line; eof.col = col;
    eof.start = ""; eof.len = 0; eof.ival = 0; eof.fval = 0; eof.lit = 0; eof.litlen = 0; eof.litoff = 0;
    emit(eof);
    const char* base = litbuf.c_str();
    for (int i = 0; i < tokens.size(); i++) {
        if (tokens[i].kind == TK_STR) tokens[i].lit = base + tokens[i].litoff;
    }
    return !error;
}

// ---- token 流转储 ----
void lexer_dump(String& out, const Lexer& lex) {
    char buf[128];
    int n = lex.count();
    for (int i = 0; i < n; i++) {
        const Token& t = lex.at(i);
        switch (t.kind) {
            case TK_INT:
                ksprintf(buf, sizeof(buf), "  [%3d] INT     %-10d  (行 %d)\n", i, (int)t.ival, t.line);
                break;
            case TK_IDENT:
                ksprintf(buf, sizeof(buf), "  [%3d] IDENT  %.*s  (行 %d)\n", i, t.len, t.start, t.line);
                break;
            default:
                ksprintf(buf, sizeof(buf), "  [%3d] %-10s  (行 %d)\n", i, tk_name(t.kind), t.line);
                break;
        }
        out += buf;
    }
}
// ---- token 分类 ----
int token_category(int kind) {
    if (kind >= KW_VOID && kind <= KW_TYPEDEF) return 0;   // 关键字区间
    if (kind == TK_IDENT) return 1;
    if (kind == TK_INT || kind == TK_FLOAT || kind == TK_CHAR || kind == TK_STR) return 2;
    if (kind >= PUN_PLUS) return 3;
    return 4;
}
// ---- 自测试 ----
int lexer_self_test() {
    int fails = 0;

    {
        Lexer l("int main(void){ return 1 + 2 * 3; }");
        bool ok = l.tokenize();
        if (!ok) { fails++; }
        else {
            if (l.at(0).kind != KW_INT) { fails++; }
            if (l.at(1).kind != TK_IDENT || !streqn(l.at(1).start, "main", 4)) fails++;
            if (l.at(2).kind != PUN_LPAREN) fails++;
            if (l.at(3).kind != KW_VOID) fails++;
            int ret_idx = -1;
            for (int i = 0; i < l.count(); i++) if (l.at(i).kind == KW_RETURN) ret_idx = i;
            if (ret_idx < 0) fails++;
            else {
                if (l.at(ret_idx + 1).kind != TK_INT || l.at(ret_idx + 1).ival != 1) fails++;
                if (l.at(ret_idx + 2).kind != PUN_PLUS) fails++;
                if (l.at(ret_idx + 4).kind != PUN_STAR) fails++;
                if (l.at(l.count() - 1).kind != TK_EOF) fails++;
            }
        }
    }

    {
        Lexer l("int a = 0x1F;\nint b = 255;");
        l.tokenize();
        int hexi = -1, deci = -1;
        for (int i = 0; i < l.count(); i++) {
            if (l.at(i).kind == TK_INT && l.at(i).ival == 31) hexi = i;
            if (l.at(i).kind == TK_INT && l.at(i).ival == 255) deci = i;
        }
        if (hexi < 0) fails++;
        if (deci < 0) fails++;
        else if (l.at(deci).line != 2) fails++;
    }

    {
        Lexer l("a >= b && c != d <<= 2");
        l.tokenize();
        bool found_ge = false, found_land = false, found_ne = false, found_lshass = false;
        for (int i = 0; i < l.count(); i++) {
            if (l.at(i).kind == PUN_GE) found_ge = true;
            if (l.at(i).kind == PUN_LAND) found_land = true;
            if (l.at(i).kind == PUN_NE) found_ne = true;
            if (l.at(i).kind == PUN_LSH_ASSIGN) found_lshass = true;
        }
        if (!found_ge || !found_land || !found_ne || !found_lshass) fails++;
    }

    // token 分类：关键字/标识符/字面量/运算符
    {
        if (token_category(KW_RETURN) != 0) fails++;
        if (token_category(TK_IDENT) != 1) fails++;
        if (token_category(TK_INT) != 2) fails++;
        if (token_category(PUN_PLUS) != 3) fails++;
    }

    // 行号追踪：多行源码第二行 token 的 line 应为 2
    {
        Lexer l("int a = 1;\nint b = 2;\n");
        l.tokenize();
        bool found_line2 = false;
        for (int i = 0; i < l.count(); i++)
            if (l.at(i).line == 2) found_line2 = true;
        if (!found_line2) fails++;
    }

    // 关键字表：全部关键字应被识别
    {
        Lexer l("int char void return if else while do for break continue");
        l.tokenize();
        int kw = 0;
        for (int i = 0; i < l.count(); i++) {
            int k = l.at(i).kind;
            if (k == KW_INT || k == KW_CHAR || k == KW_VOID || k == KW_RETURN ||
                k == KW_IF || k == KW_ELSE || k == KW_WHILE || k == KW_DO ||
                k == KW_FOR || k == KW_BREAK || k == KW_CONTINUE) kw++;
        }
        if (kw != 11) fails++;
    }

    {
        Lexer l("char *s = \"a\\tb\\n\";");
        l.tokenize();
        int str_idx = -1;
        for (int i = 0; i < l.count(); i++) if (l.at(i).kind == TK_STR) str_idx = i;
        if (str_idx < 0) fails++;
        else {
            const Token& t = l.at(str_idx);
            if (t.litlen != 4) fails++;
            else if (t.lit[0] != 'a' || t.lit[1] != '\t' || t.lit[2] != 'b' || t.lit[3] != '\n') fails++;
        }
    }

    {
        Lexer l("'A' '\\n' '0'");
        l.tokenize();
        if (l.count() < 4) { fails++; }
        else {
            if (l.at(0).kind != TK_CHAR || l.at(0).ival != 65) fails++;
            if (l.at(1).kind != TK_CHAR || l.at(1).ival != 10) fails++;
            if (l.at(2).kind != TK_CHAR || l.at(2).ival != 48) fails++;
        }
    }

    {
        Lexer l("/* block */ int x = 5; // tail\nx;");
        l.tokenize();
        bool found_int = false;
        for (int i = 0; i < l.count(); i++) {
            if (l.at(i).kind == KW_INT) found_int = true;
        }
        if (!found_int) fails++;
        for (int i = 0; i < l.count(); i++) {
            if (l.at(i).kind == TK_IDENT && streqn(l.at(i).start, "block", 5)) fails++;
        }
    }

    {
        Lexer l("float f = 3.14;");
        l.tokenize();
        bool found_float = false;
        for (int i = 0; i < l.count(); i++) {
            if (l.at(i).kind == TK_FLOAT) {
                found_float = true;
                if (l.at(i).fval < 3.13 || l.at(i).fval > 3.15) fails++;
            }
        }
        if (!found_float) fails++;
    }

    // token 转储不崩溃且含内容
    {
        Lexer l("int x = 1 + 2;");
        l.tokenize();
        String d;
        lexer_dump(d, l);
        if (d.len() < 4) fails++;
    }
    return fails;
}

} // namespace compiler
} // namespace nefu
