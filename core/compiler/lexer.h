// ============================================================================
// nefu::compiler —— C 子集词法分析器（lexer.h）
// ----------------------------------------------------------------------------
// 把 C 子集源码切成 token 序列。支持：
//   * 关键字：void char short int long float double const static extern sizeof
//             if else while do for return break continue struct typedef
//   * 标识符 / 整数（十进制、0x 十六进制、0 八进制）/ 浮点 / 字符 / 字符串
//   * 运算符：单字符与双字符（== != <= >= && || ++ -- -> += -= ... << >>）
//   * 注释：// 行注释 与 /* 块注释 */
//   * 行号 / 列号追踪，用于报错定位
// 词法错误时记录到 errmsg 并产出 TK_ERR token。
// 注意：本模块禁止 STL / 异常 / RTTI / malloc，仅用 nefu::List / nefu::String。
// ============================================================================
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace compiler {

// ---- token 种类枚举 ----
enum Tk {
    TK_EOF = 0,     // 源码结束
    TK_ERR,         // 词法错误
    TK_IDENT,       // 标识符
    TK_INT,         // 整数字面量
    TK_FLOAT,       // 浮点字面量
    TK_CHAR,        // 字符字面量
    TK_STR,         // 字符串字面量
    // ---- 关键字 ----
    KW_VOID, KW_CHAR, KW_SHORT, KW_INT, KW_LONG, KW_FLOAT, KW_DOUBLE,
    KW_CONST, KW_STATIC, KW_EXTERN, KW_SIZEOF,
    KW_IF, KW_ELSE, KW_WHILE, KW_DO, KW_FOR,
    KW_RETURN, KW_BREAK, KW_CONTINUE,
    KW_STRUCT, KW_TYPEDEF,
    // ---- 双字符 / 复合运算符（先于单字符匹配）----
    PUN_LSH, PUN_RSH,           // <<  >>
    PUN_INC, PUN_DEC,           // ++  --
    PUN_ARROW,                  // ->
    PUN_LAND, PUN_LOR,          // &&  ||
    PUN_LE, PUN_GE, PUN_EQ, PUN_NE,   // <= >= == !=
    PUN_ADD_ASSIGN, PUN_SUB_ASSIGN,    // +=  -=
    PUN_MUL_ASSIGN, PUN_DIV_ASSIGN,    // *=  /=
    PUN_MOD_ASSIGN,                    // %=
    PUN_AND_ASSIGN, PUN_OR_ASSIGN, PUN_XOR_ASSIGN,  // &=  |=  ^=
    PUN_LSH_ASSIGN, PUN_RSH_ASSIGN,   // <<=  >>=
    // ---- 单字符运算符 / 标点 ----
    PUN_PLUS, PUN_MINUS, PUN_STAR, PUN_SLASH, PUN_PERCENT,
    PUN_AMP, PUN_PIPE, PUN_CARET, PUN_TILDE, PUN_BANG,
    PUN_ASSIGN,                 // =
    PUN_LT, PUN_GT,             // <  >
    PUN_QUEST, PUN_COLON,       // ?  :
    PUN_SEMI, PUN_COMMA, PUN_DOT,  // ;  ,  .
    PUN_LPAREN, PUN_RPAREN,
    PUN_LBRACE, PUN_RBRACE,
    PUN_LBRACKET, PUN_RBRACKET
};

// ---- 一个 token ----
struct Token {
    int   kind;         // Tk
    int   line;         // 1 基行号
    int   col;          // 1 基列号
    const char* start;  // 指向源码（标识符 / 数字用），不拥有
    int   len;          // 文本长度
    int64_t  ival;      // TK_INT 的值；TK_CHAR 存字符码
    double   fval;      // TK_FLOAT 的值
    const char* lit;    // TK_STR / TK_CHAR 解码后的字面量（指向 Lexer 的字面量缓冲）
    int      litlen;
    int      litoff;    // TK_STR 在 litbuf 中的起始偏移（tokenize 结束后用于回填 lit）
};

// ---- 词法分析器 ----
struct Lexer {
    const char* src;    // 源码（不拥有）
    int   pos;          // 当前偏移
    int   line;         // 当前行
    int   col;          // 当前列
    bool  error;        // 是否发生词法错误
    String errmsg;      // 错误描述
    List<Token> tokens; // 产出的 token 序列
    String litbuf;      // 字符串 / 字符解码结果缓冲（按追加方式存放）

    Lexer();
    explicit Lexer(const char* source);
    void reset(const char* source);

    // 扫描整段源码，结果写入 tokens；返回 false 表示词法错误
    bool tokenize();

    // 取第 i 个 token（越界返回 EOF）
    const Token& at(int i) const { return (i >= 0 && i < tokens.size()) ? tokens[i] : eof_tok; }
    int count() const { return tokens.size(); }

private:
    Token eof_tok;
public:
    char  cur() const;          // 当前字符，0 表示结束
    char  peekch(int off = 0) const;
    char  advance();
private:
    void  skip_trivia();        // 空白与注释
    Token read_ident();
    Token read_number();
    Token read_char();
    Token read_string();
    int   keyword_kind(const char* s, int n) const;
    void  emit(const Token& t);
    static bool is_id_start(char c);
    static bool is_id_char(char c);
};

// 把 token kind 转成可读名字（报错 / 调试用）
const char* tk_name(int kind);

// token 分类：0=关键字 1=标识符 2=字面量 3=运算符/标点 4=其他
int token_category(int kind);

// 把整个 token 流写成可读文本（供窗口应用 / 调试展示）
void lexer_dump(String& out, const Lexer& lex);

// 词法器自测试：返回失败数（0 表示全部通过）
int lexer_self_test();

} // namespace compiler
} // namespace nefu
