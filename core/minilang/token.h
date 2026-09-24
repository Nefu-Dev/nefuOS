// ============================================================================
// nefu::minilang —— 词法分析器（token.h）
// ----------------------------------------------------------------------------
// 把源代码字符串切成 token 序列。支持：
//   * 关键字：let fn if else while for return true false null and or not
//   * 标识符：字母/下划线开头，后接字母数字下划线
//   * 数字：十进制整数；带小数点的视为定点数（如 3.14）
//   * 字符串：双引号包裹，支持 \n \t \" \\ \0 转义
//   * 运算符：+ - * / % = == != 小于 大于 <= >= ! && || ( ) { } [ ] , ; .
//   * 注释：// 行注释 与 /* 块注释 */
// 词法错误时设置 Lexer::error 与 errmsg，并产出 EOF token。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "../lib/softmath.h"

namespace nefu {
namespace minilang {

// token 种类
enum TokenKind {
    TOK_EOF = 0,
    TOK_ERR,
    // 字面量
    TOK_INT,        // 整数
    TOK_FLOAT,      // 定点数字面量（带小数点）
    TOK_STR,        // 字符串字面量（已处理转义）
    TOK_IDENT,      // 标识符
    // 关键字
    TOK_LET, TOK_FN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_AND, TOK_OR, TOK_NOT,
    // 运算符 / 标点
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN,                 // =
    TOK_EQ, TOK_NEQ,            // == !=
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_BANG,                   // !
    TOK_AND_AND, TOK_OR_OR,     // && ||
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMI, TOK_DOT
};

// 一个 token：kind + 文本片段（存在 Lexer 的字符串缓冲里，不单独分配）
struct Token {
    TokenKind kind;
    const char* text;   // 指向源码或 Lexer 的字符串缓冲（TOK_STR 时）
    int   len;
    int   line;         // 1 基行号，用于报错
    int64_t intval;     // TOK_INT 时的整数值
    fx::fix fxval;      // TOK_FLOAT 时的 Q16.16 值
};

// 词法分析器：持有源码指针，逐个产出 token。不拷贝源码。
struct Lexer {
    const char* src;
    int   pos;
    int   line;
    bool  error;
    String errmsg;

    // 字符串字面量的转义结果缓冲（避免每个字符串 new 一块）
    String strbuf;

    Lexer(const char* source);
    void reset(const char* source);

    Token next();           // 取下一个 token；遇到末尾返回 TOK_EOF
    Token peek();           // 不消耗地看下一个 token（缓存一个）
    bool  has_peek;
    Token peeked;

private:
    char  cur() const;      // 当前字符（0 表示结束）
    char  advance();
    void  skip_trivia();    // 空白与注释
    Token read_number();
    Token read_hex();
    Token read_ident();
    Token read_string();
    TokenKind keyword_kind(const char* s, int len) const;
};

// 把 token kind 转成可读名字（报错用）
const char* token_kind_name(TokenKind k);

// 词法器自测试：返回失败数
int token_self_test();

} // namespace minilang
} // namespace nefu

