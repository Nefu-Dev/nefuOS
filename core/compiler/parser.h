// ============================================================================
// nefu::compiler —— 递归下降解析器（parser.h）
// ----------------------------------------------------------------------------
// 把词法 token 流解析成 AST。支持 C 子集：
//   * 函数定义 / 声明（返回类型 + 参数列表）
//   * 局部变量声明（带初始化式）
//   * if/else while for return break continue
//   * 表达式：完整优先级爬升（赋值 / 三目 / 逻辑 / 关系 / 加减 / 乘除 / 单目 / 后缀）
//   * 类型系统：void char int long + 任意层指针
// 解析错误写入 CompContext::err，返回尽可能多的已解析内容。
// ============================================================================
#pragma once

#include "lexer.h"
#include "ast.h"

namespace nefu {
namespace compiler {

struct Parser {
    CompContext& cc;
    Lexer& lex;
    int pos;            // 当前 token 下标

    Parser(CompContext& c, Lexer& l);

    // 解析整段翻译单元，返回 ProgramNode
    ProgramNode* parse();

    const Token& cur_tok() const;
    const Token& peek_tok(int off = 0) const;
    bool check(int kind) const;
    bool accept(int kind);          // 匹配则消费并返回 true
    void expect(int kind);          // 消费，不匹配则报错
    int tok_op() const;             // 把标点 token 映射成 Op

private:
    Type* parse_type_specifier();   // 基础类型 + const/static/extern 前缀
    Type* parse_declarator(Type* base, const char*& out_name, int& out_namelen,
                           List<const char*>* params);
    Node* parse_external();
    Node* parse_statement();
    Block* parse_compound();
    Node* parse_expression();       // 入口 = 赋值级
    Node* parse_assign();
    Node* parse_ternary();
    Node* parse_logical_or();
    Node* parse_logical_and();
    Node* parse_bit_or();
    Node* parse_bit_xor();
    Node* parse_bit_and();
    Node* parse_equality();
    Node* parse_relational();
    Node* parse_shift();
    Node* parse_additive();
    Node* parse_multiplicative();
    Node* parse_unary();
    Node* parse_postfix();
    Node* parse_primary();
};

// 便捷入口：对一段源码走完整 词法 -> 语法，返回 ProgramNode（错误写在 cc）
ProgramNode* compile_parse(CompContext& cc, const char* source);

// 解析器自测试：返回失败数
int parser_self_test();

} // namespace compiler
} // namespace nefu
