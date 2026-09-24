// ============================================================================
// nefu::minilang —— 词法分析器（实现）
// ============================================================================
#include "token.h"
#include <string.h>

namespace nefu {
namespace minilang {

Lexer::Lexer(const char* source)
    : src(source ? source : ""), pos(0), line(1),
      error(false), errmsg(), has_peek(false), peeked() {
}

void Lexer::reset(const char* source) {
    src = source ? source : "";
    pos = 0; line = 1;
    error = false;
    errmsg.clear();
    strbuf.clear();
    has_peek = false;
}

char Lexer::cur() const { return src[pos]; }

char Lexer::advance() {
    char c = src[pos];
    if (c == 0) return 0;
    pos++;
    if (c == '\n') line++;
    return c;
}

// 跳过空白与注释
void Lexer::skip_trivia() {
    for (;;) {
        char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && src[pos + 1] == '/') {
            // 行注释：吃到行尾
            while (cur() != 0 && cur() != '\n') advance();
            continue;
        }
        if (c == '/' && src[pos + 1] == '*') {
            advance(); advance();
            while (cur() != 0 && !(cur() == '*' && src[pos + 1] == '/')) advance();
            if (cur() == '*') { advance(); advance(); }
            continue;
        }
        break;
    }
}

TokenKind Lexer::keyword_kind(const char* s, int len) const {
    char buf[16];
    int n = len < 15 ? len : 15;
    for (int i = 0; i < n; i++) buf[i] = s[i];
    buf[n] = 0;
    struct KW { const char* w; TokenKind k; };
    static const KW table[] = {
        {"let",TOK_LET}, {"fn",TOK_FN}, {"if",TOK_IF}, {"else",TOK_ELSE},
        {"while",TOK_WHILE}, {"for",TOK_FOR}, {"return",TOK_RETURN},
        {"true",TOK_TRUE}, {"false",TOK_FALSE}, {"null",TOK_NULL},
        {"and",TOK_AND}, {"or",TOK_OR}, {"not",TOK_NOT}
    };
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])); i++) {
        if ((int)strlen(table[i].w) == len && strcmp(table[i].w, buf) == 0)
            return table[i].k;
    }
    return TOK_IDENT;
}

// 数字：整数或带小数点的定点数
Token Lexer::read_number() {
    Token t; t.kind = TOK_INT; t.text = src + pos; t.line = line;
    int64_t whole = 0;
    while (cur() >= '0' && cur() <= '9') {
        whole = whole * 10 + (cur() - '0');
        advance();
    }
    fx::fix frac = 0;
    if (cur() == '.' ) {
        t.kind = TOK_FLOAT;
        advance();
        int32_t fdig = 0, scale = 1;
        while (cur() >= '0' && cur() <= '9' && scale < 100000) {
            fdig = fdig * 10 + (cur() - '0');
            scale *= 10;
            advance();
        }
        frac = fx::itofix(0);
        frac += (fx::fix)(((int64_t)fdig * fx::FX_ONE) / scale);
    }
    t.intval = whole;
    t.fxval = fx::itofix((int)whole) + frac;
    t.len = (int)((src + pos) - t.text);
    return t;
}

// 十六进制数字：0x1F 形式
Token Lexer::read_hex() {
    Token t; t.kind = TOK_INT; t.text = src + pos; t.line = line;
    advance(); advance();   // 吃掉 0x
    int64_t v = 0;
    for (;;) {
        char c = cur();
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) break;
        v = v * 16 + d;
        advance();
    }
    t.intval = v;
    t.fxval = fx::itofix((int)v);
    t.len = (int)((src + pos) - t.text);
    return t;
}

// 标识符 / 关键字
Token Lexer::read_ident() {
    Token t; t.text = src + pos; t.line = line;
    while (true) {
        char c = cur();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') advance();
        else break;
    }
    t.len = (int)((src + pos) - t.text);
    t.kind = keyword_kind(t.text, t.len);
    return t;
}

// 字符串字面量，处理转义，结果存入 strbuf
Token Lexer::read_string() {
    Token t; t.line = line;
    advance(); // 跳过开场引号
    strbuf.clear();
    while (cur() != 0 && cur() != '"') {
        char c = advance();
        if (c == '\\') {
            char e = advance();
            switch (e) {
            case 'n': strbuf += '\n'; break;
            case 't': strbuf += '\t'; break;
            case 'r': strbuf += '\r'; break;
            case '"': strbuf += '"'; break;
            case '\\': strbuf += '\\'; break;
            case '0': strbuf += '\0'; break;
            default: strbuf += e; break;
            }
        } else {
            strbuf += c;
        }
    }
    if (cur() == '"') advance();
    else {
        error = true;
        errmsg = "unterminated string";
    }
    t.kind = TOK_STR;
    t.text = strbuf.c_str();
    t.len = strbuf.len();
    return t;
}

Token Lexer::next() {
    if (has_peek) { has_peek = false; return peeked; }
    skip_trivia();
    char c = cur();
    if (c == 0) { Token t; t.kind = TOK_EOF; t.text = ""; t.len = 0; t.line = line; return t; }

    // 数字
    if (c >= '0' && c <= '9') {
        if (c == '0' && (src[pos+1] == 'x' || src[pos+1] == 'X')) return read_hex();
        return read_number();
    }
    // 标识符
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return read_ident();
    // 字符串
    if (c == '"') return read_string();

    // 双字符运算符优先
    advance();
    switch (c) {
    case '+': { Token t{TOK_PLUS, 0, 1, line, 0, 0}; return t; }
    case '-': { Token t{TOK_MINUS, 0, 1, line, 0, 0}; return t; }
    case '*': { Token t{TOK_STAR, 0, 1, line, 0, 0}; return t; }
    case '/': { Token t{TOK_SLASH, 0, 1, line, 0, 0}; return t; }
    case '%': { Token t{TOK_PERCENT, 0, 1, line, 0, 0}; return t; }
    case '(': { Token t{TOK_LPAREN, 0, 1, line, 0, 0}; return t; }
    case ')': { Token t{TOK_RPAREN, 0, 1, line, 0, 0}; return t; }
    case '{': { Token t{TOK_LBRACE, 0, 1, line, 0, 0}; return t; }
    case '}': { Token t{TOK_RBRACE, 0, 1, line, 0, 0}; return t; }
    case '[': { Token t{TOK_LBRACKET, 0, 1, line, 0, 0}; return t; }
    case ']': { Token t{TOK_RBRACKET, 0, 1, line, 0, 0}; return t; }
    case ',': { Token t{TOK_COMMA, 0, 1, line, 0, 0}; return t; }
    case ';': { Token t{TOK_SEMI, 0, 1, line, 0, 0}; return t; }
    case '.': { Token t{TOK_DOT, 0, 1, line, 0, 0}; return t; }
    }

    // 需要看第二个字符的
    if (c == '=') {
        if (cur() == '=') { advance(); Token t{TOK_EQ, 0, 2, line, 0, 0}; return t; }
        Token t{TOK_ASSIGN, 0, 1, line, 0, 0}; return t;
    }
    if (c == '!') {
        if (cur() == '=') { advance(); Token t{TOK_NEQ, 0, 2, line, 0, 0}; return t; }
        Token t{TOK_BANG, 0, 1, line, 0, 0}; return t;
    }
    if (c == '<') {
        if (cur() == '=') { advance(); Token t{TOK_LE, 0, 2, line, 0, 0}; return t; }
        Token t{TOK_LT, 0, 1, line, 0, 0}; return t;
    }
    if (c == '>') {
        if (cur() == '=') { advance(); Token t{TOK_GE, 0, 2, line, 0, 0}; return t; }
        Token t{TOK_GT, 0, 1, line, 0, 0}; return t;
    }
    if (c == '&' && cur() == '&') { advance(); Token t{TOK_AND_AND, 0, 2, line, 0, 0}; return t; }
    if (c == '|' && cur() == '|') { advance(); Token t{TOK_OR_OR, 0, 2, line, 0, 0}; return t; }

    error = true;
    errmsg = "unexpected character";
    Token t; t.kind = TOK_ERR; t.text = ""; t.len = 0; t.line = line;
    return t;
}

Token Lexer::peek() {
    if (!has_peek) { peeked = next(); has_peek = true; }
    return peeked;
}

const char* token_kind_name(TokenKind k) {
    switch (k) {
    case TOK_EOF: return "eof";
    case TOK_ERR: return "error";
    case TOK_INT: return "int";
    case TOK_FLOAT: return "float";
    case TOK_STR: return "string";
    case TOK_IDENT: return "ident";
    case TOK_LET: return "let";
    case TOK_FN: return "fn";
    case TOK_IF: return "if";
    case TOK_ELSE: return "else";
    case TOK_WHILE: return "while";
    case TOK_FOR: return "for";
    case TOK_RETURN: return "return";
    case TOK_TRUE: return "true";
    case TOK_FALSE: return "false";
    case TOK_NULL: return "null";
    case TOK_AND: return "and";
    case TOK_OR: return "or";
    case TOK_NOT: return "not";
    case TOK_PLUS: return "+";
    case TOK_MINUS: return "-";
    case TOK_STAR: return "*";
    case TOK_SLASH: return "/";
    case TOK_PERCENT: return "%";
    case TOK_ASSIGN: return "=";
    case TOK_EQ: return "==";
    case TOK_NEQ: return "!=";
    case TOK_LT: return "<";
    case TOK_GT: return ">";
    case TOK_LE: return "<=";
    case TOK_GE: return ">=";
    case TOK_BANG: return "!";
    case TOK_AND_AND: return "andand";
    case TOK_OR_OR: return "oror";
    case TOK_LPAREN: return "(";
    case TOK_RPAREN: return ")";
    case TOK_LBRACE: return "{";
    case TOK_RBRACE: return "}";
    case TOK_LBRACKET: return "[";
    case TOK_RBRACKET: return "]";
    case TOK_COMMA: return ",";
    case TOK_SEMI: return ";";
    case TOK_DOT: return ".";
    }
    return "?";
}

int token_self_test() {
    int fails = 0;

    // 1) 关键字与标识符
    {
        Lexer l("let fn if else while for return true false null and or not foo");
        TokenKind expect[] = {
            TOK_LET, TOK_FN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN,
            TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_AND, TOK_OR, TOK_NOT, TOK_IDENT
        };
        for (int i = 0; i < 14; i++) {
            Token t = l.next();
            if (t.kind != expect[i]) fails++;
        }
        if (l.next().kind != TOK_EOF) fails++;
    }

    // 2) 数字
    {
        Lexer l("123 3.14");
        Token a = l.next();
        if (a.kind != TOK_INT || a.intval != 123) fails++;
        Token b = l.next();
        if (b.kind != TOK_FLOAT) fails++;
        if (fx::fixtoi(b.fxval) != 3) fails++;
    }

    // 3) 字符串与转义
    {
        Lexer l("\"ab\\tc\"");
        Token s = l.next();
        if (s.kind != TOK_STR) fails++;
        if (s.len != 4) fails++;        // a b \t c
        if (s.text[2] != '\t') fails++;
    }

    // 4) 运算符双字符
    {
        Lexer l("== != <= >= && || = ==");
        TokenKind expect[] = { TOK_EQ, TOK_NEQ, TOK_LE, TOK_GE, TOK_AND_AND,
                               TOK_OR_OR, TOK_ASSIGN, TOK_EQ };
        for (int i = 0; i < 8; i++) {
            Token t = l.next();
            if (t.kind != expect[i]) fails++;
        }
    }

    // 5) 注释被跳过
    {
        Lexer l("1 // 行注释\n2 /* 块\n注释 */ 3");
        Token a = l.next(); if (a.intval != 1) fails++;
        Token b = l.next(); if (b.intval != 2) fails++;
        Token c = l.next(); if (c.intval != 3) fails++;
    }

    // 6) peek 不消耗
    {
        Lexer l("1 2");
        Token p = l.peek();
        if (p.intval != 1) fails++;
        Token a = l.next();
        if (a.intval != 1) fails++;
        Token b = l.next();
        if (b.intval != 2) fails++;
    }

    // 7) 十六进制字面量
    {
        Lexer l("0x1F 0xFF 0x0");
        Token a = l.next(); if (a.kind != TOK_INT || a.intval != 31) fails++;
        Token b = l.next(); if (b.kind != TOK_INT || b.intval != 255) fails++;
        Token c = l.next(); if (c.kind != TOK_INT || c.intval != 0) fails++;
    }

    return fails;
}

} // namespace minilang
} // namespace nefu

