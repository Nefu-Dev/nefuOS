// ============================================================================
// nefu::minilang —— 递归下降解析器（parser.h）
// ----------------------------------------------------------------------------
// 文法（优先级从低到高）：
//   program   -> stmt* EOF
//   stmt      -> let | if | while | for | return | block | exprstmt
//   block     -> "{" stmt* "}"
//   expr      -> or
//   or        -> and ( ("or"|"||") and )*
//   and       -> eq ( ("and"|"&&") eq )*
//   eq        -> cmp ( ("=="|"!=") cmp )*
//   cmp       -> add ( ("<"|">"|"<="|">=") add )*
//   add       -> mul ( ("+"|"-") mul )*
//   mul       -> unary ( ("*"|"/"|"%") unary )*
//   unary     -> ("!"|"-") unary | postfix
//   postfix   -> primary ( "(" args ")" | "[" expr "]" )*
//   primary   -> INT | FLOAT | STR | TRUE | FALSE | NULL | IDENT |
//                "[" (expr ("," expr)*)? "]" | "(" expr ")"
// 解析失败时设置 error/errmsg，返回已解析的部分程序（调用方应检查 error）。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "token.h"
#include "ast.h"

namespace nefu {
namespace minilang {

struct Parser {
    Lexer lex;
    bool  error;
    String errmsg;
    ProgramObj* prog;     // 当前正在构建的程序（不持有所有权，由调用方给）

    Parser(const char* source);

    // 解析整段源码；在 prog 上填充 stmts。失败返回 false 并写 errmsg。
    bool parse_program(ProgramObj* prog);

private:
    Token cur;            // 当前已取 token
    void  advance();      // cur = lex.next()
    bool  accept(TokenKind k);  // 若是 k 则消耗并返回 true
    bool  expect(TokenKind k, const char* what);  // 期望，否则报错

    Node* parse_stmt();
    Node* parse_block();
    Node* parse_let();
    Node* parse_if();
    Node* parse_while();
    Node* parse_for();
    Node* parse_return();
    Node* parse_exprstmt();

    Node* parse_expr();
    Node* parse_or();
    Node* parse_and();
    Node* parse_eq();
    Node* parse_cmp();
    Node* parse_add();
    Node* parse_mul();
    Node* parse_unary();
    Node* parse_postfix();
    Node* parse_primary();

    void fail(const char* msg);
};

// 便捷入口：解析源码，返回一个 program Value（裸引用，调用方 drop）。
// 解析失败时返回 null Value 并把错误写到 errmsg_out。
Value minilang_parse(const char* source, String* errmsg_out);

// 自测试
int parser_self_test();

} // namespace minilang
} // namespace nefu
