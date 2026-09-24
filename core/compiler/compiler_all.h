// ============================================================================
// nefu::compiler —— 聚合头（compiler_all.h）
// ----------------------------------------------------------------------------
// 一次包含全部编译器工具链模块，并提供：
//   * compiler_self_test()：汇总所有模块的自测试失败数；
//   * compiler_pipeline()：完整走一遍 预处理->词法->语法->IR->汇编，
//     把各阶段产物文本写进 out，供窗口应用展示。
//   * compiler_stats()：输出各阶段节点 / 指令统计，用于教学演示。
//
// 模块划分（每个模块独立编译并自带 xxx_self_test()）：
//   lexer        C 子集词法分析：关键字、标识符、整/浮点、字符、字符串、
//                运算符、注释、行号追踪。
//   ast          抽象语法树节点定义 + 多 chunk arena 分配 + 打印器。
//   parser       递归下降解析：函数、变量、if/else、while、do-while、for、
//                return、表达式优先级、类型系统、错误恢复。
//   ir           三地址中间代码：基本块、指令、常量、phi、分支/跳转，
//                附带校验、活跃分析、常量折叠、死代码删除。
//   codegen      IR -> x86-64 AT&T 汇编：线性扫描寄存器分配、栈帧、调用约定。
//   assembler    汇编器：指令字节编码、标签解析、重定位、迷你 ELF 对象。
//   linker       迷你链接器：多对象合并、符号解析、重定位应用。
//   preprocessor C 预处理：对象/带参宏、条件编译、#if/#elif/#error、行续接。
//
// 约束：禁用 STL 容器 / 异常 / RTTI / malloc；统一用 nefu::List、nefu::String、
// kalloc/kfree 与 new[]/delete[]；MinGW -O2 下用手动字节循环避免 memset 误优化。
// 本头文件不产生代码，仅做聚合与 inline 自测求和。
// ============================================================================
#pragma once

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "ir.h"
#include "codegen.h"
#include "assembler.h"
#include "linker.h"
#include "preprocessor.h"

namespace nefu {
namespace compiler {

// 汇总所有模块自测试，返回总失败数（0 表示全部通过）
inline int compiler_self_test() {
    int fails = 0;
    fails += lexer_self_test();
    fails += ast_self_test();
    fails += parser_self_test();
    fails += ir_self_test();
    fails += codegen_self_test();
    fails += assembler_self_test();
    fails += linker_self_test();
    fails += preprocessor_self_test();
    return fails;
}

// 编译器统计：词法/语法/IR 各阶段计数，写入 out，返回错误数。
inline int compiler_stats(const char* source, String& out) {
    CompContext cc;
    Preprocessor pp;
    const char* src = pp.process(source);

    Lexer lex(src);
    lex.tokenize();

    ProgramNode* prog = compile_parse(cc, src);
    int nfuncs = 0, nvars = 0;
    if (prog) {
        for (int i = 0; i < prog->decls.size(); i++)
            if (prog->decls[i]->kind == N_FUNC) nfuncs++;
    }
    IrModule* mod = ir_gen_program(cc, prog);
    int ninstr = 0;
    if (mod) for (int i = 0; i < mod->funcs.size(); i++) ninstr += mod->funcs[i]->code.size();

    String asmtext = codegen_module(mod);
    char buf[160];
    ksprintf(buf, sizeof(buf), "tokens=%d  functions=%d  ir_instructions=%d  asm_chars=%d  parse_errors=%d\n",
             lex.count(), nfuncs, ninstr, asmtext.len(), cc.err_count);
    out += buf;
    return cc.err_count;
}

// 完整流水线：对一段 C 子集源码做预处理->词法->语法->IR->汇编，
// 把各阶段文本依次追加到 out。返回错误数（0 表示成功）。
inline int compiler_pipeline(const char* source, String& out) {
    CompContext cc;

    // 1) 预处理
    Preprocessor pp;
    const char* src = pp.process(source);

    out += "===== 预处理后源码 =====\n";
    out += src;
    out += "\n";

    // 2) 词法分析
    Lexer lex(src);
    lex.tokenize();
    out += "===== 词法 Token 流 =====\n";
    for (int i = 0; i < lex.count(); i++) {
        const Token& t = lex.at(i);
        char buf[96];
        if (t.kind == TK_IDENT || t.kind == TK_INT) {
            ksprintf(buf, sizeof(buf), "  [%2d] %-10s  '%.*s'  (行 %d)\n",
                     i, tk_name(t.kind), t.len, t.start, t.line);
        } else {
            ksprintf(buf, sizeof(buf), "  [%2d] %-10s  (行 %d)\n",
                     i, tk_name(t.kind), t.line);
        }
        out += buf;
    }
    out += "\n";

    // 3) 语法分析 -> AST
    ProgramNode* prog = compile_parse(cc, src);
    out += "===== 抽象语法树 (AST) =====\n";
    ast_dump(out, prog);
    out += "\n";

    // 3.5) 语义分析
    out += "===== 语义分析 =====\n";
    String semrep;
    int semerr = sem_analyze(prog, semrep);
    if (semerr == 0) out += "  (无错误)\n";
    else out += semrep;
    out += "\n";

    // 4) IR
    IrModule* mod = ir_gen_program(cc, prog);
    out += "===== 三地址中间代码 (IR) =====\n";
    ir_dump(out, mod);
    out += "\n";

    // 4.4) 基本块切分（控制流教学演示）
    out += "===== 基本块 (basic blocks) =====\n";
    for (int fi = 0; fi < mod->funcs.size(); fi++) {
        String bb;
        int nblk = ir_basic_blocks(mod->funcs[fi], bb);
        char bh[64];
        ksprintf(bh, sizeof(bh), "  函数 %s: %d 个基本块\n",
                 mod->funcs[fi]->name ? mod->funcs[fi]->name : "?", nblk);
        out += bh;
        out += bb;
    }
    out += "\n";

    // 4.45) 控制流图边
    out += "===== 控制流图边 (CFG edges) =====\n";
    for (int fi = 0; fi < mod->funcs.size(); fi++) {
        String ce;
        int ne = ir_cfg_edges(mod->funcs[fi], ce);
        char ch[64];
        ksprintf(ch, sizeof(ch), "  函数 %s: %d 条边\n",
                 mod->funcs[fi]->name ? mod->funcs[fi]->name : "?", ne);
        out += ch;
        out += ce;
    }
    out += "\n";

    // 4.5) 活跃区间（寄存器分配教学演示）
    out += "===== 活跃区间 (live intervals) =====\n";
    for (int fi = 0; fi < mod->funcs.size(); fi++) {
        String iv;
        int nint = ir_live_intervals(mod->funcs[fi], iv);
        char hdr[64];
        ksprintf(hdr, sizeof(hdr), "  函数 %s: %d 个区间\n",
                 mod->funcs[fi]->name ? mod->funcs[fi]->name : "?", nint);
        out += hdr;
        out += iv;
    }
    out += "\n";

    // 5) 代码生成 -> 汇编文本
    out += "===== x86-64 汇编 (AT&T) =====\n";
    String asmtext = codegen_module(mod);
    out += asmtext;

    int errs = cc.err_count;
    delete mod;
    return errs;
}

} // namespace compiler
} // namespace nefu
