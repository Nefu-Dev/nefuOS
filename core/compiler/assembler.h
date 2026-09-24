// ============================================================================
// nefu::compiler —— 迷你 x86-64 汇编器（assembler.h）
// ----------------------------------------------------------------------------
// 接收内部 AsmInstr 指令流，两遍扫描：
//   第一遍：计算每条指令的长度与标签地址；
//   第二遍：把操作码 / ModR/M / 立即数编码成字节，记录重定位项。
// 输出一个 ObjectFile（代码字节 + 符号表 + 重定位表），供链接器消费。
// 覆盖常用指令：nop / ret / push / pop / mov imm / add / sub / call / jmp /
//               条件跳转 / 算术移位。
// ============================================================================
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace compiler {

// ---- 指令助记符 ----
enum AsmMn {
    ASM_NOP = 0,
    ASM_RET,
    ASM_PUSH_RBP,
    ASM_POP_RBP,
    ASM_MOV_RBP_RSP,
    ASM_SUB_RSP_IMM,     // sub $imm, %rsp
    ASM_MOV_REG_IMM,     // mov $imm, %reg
    ASM_MOV_REG_STACK,   // mov disp(%rbp), %reg
    ASM_MOV_STACK_REG,   // mov %reg, disp(%rbp)
    ASM_ADD_REG,         // add %reg, %eax
    ASM_SUB_REG,
    ASM_IMUL_REG,
    ASM_CMP_STACK,       // cmp disp(%rbp), %eax
    ASM_CALL,            // call rel32 / 符号
    ASM_JMP_LABEL,       // jmp rel32
    ASM_JCC_LABEL,       // 条件跳转 rel8/rel32
    ASM_LEAVE,
    ASM_GLOB,            // 伪指令：符号声明
    ASM_LABEL,           // 伪指令：标签锚点
    ASM_MOV_REG_REG,     // mov %reg, %reg2
    ASM_LEA,            // lea disp(%rbp), %reg
    ASM_ADD_REG_IMM,    // add $imm, %reg
    ASM_SUB_REG_IMM,    // sub $imm, %reg
    ASM_PUSH_REG,       // push %reg
    ASM_POP_REG,        // pop %reg
    ASM_TEST_REG,       // test %reg, %reg
    ASM_NOP_WORD        // 多字节 nop 填充
};

// x86-64 寄存器编码（寄存器号，用于 ModR/M reg 字段）
enum AsmReg {
    REG_RAX = 0, REG_RCX = 1, REG_RDX = 2, REG_RBX = 3,
    REG_RSP = 4, REG_RBP = 5, REG_RSI = 6, REG_RDI = 7
};

struct AsmInstr {
    int mn;             // AsmMn
    int reg;            // AsmReg
    int64_t imm;        // 立即数 / 栈偏移
    int label;         // 标签号
    const char* sym;   // ASM_CALL / ASM_GLOB 符号名
    int cc;            // ASM_JCC 条件码（0=e 1=ne 2=l 3=le 4=g 5=ge）
};

// ---- 重定位项 ----
struct Reloc {
    int offset;         // 在代码段中的偏移
    const char* sym;    // 目标符号
    int addend;
};

// ---- 符号表项 ----
struct Symbol {
    const char* name;
    int offset;         // 段内偏移
    int kind;           // 0=函数 1=数据
};

// ---- 输出对象文件 ----
struct ObjectFile {
    uint8_t* code;
    int code_len;
    int code_cap;
    uint8_t* data;          // .data 段字节
    int data_len;
    int data_cap;
    List<Reloc> relocs;
    List<Symbol> syms;

    ObjectFile();
    ~ObjectFile();
    void emit_byte(int b);
    void emit32(int32_t v);
    void emit64(int64_t v);
    void emit_data_byte(int b);   // 写入 .data 段
    int  data_checksum();          // .data 段字节求和（教学/自校验）
};

// ---- 汇编器 ----
struct Assembler {
    List<AsmInstr> instrs;
    ObjectFile obj;
    List<int> label_addr;   // 标签号 -> 地址

    void add(const AsmInstr& in);
    int instr_size(const AsmInstr& in);    // 单条指令编码长度
    void assemble();                        // 两遍扫描产出 obj
};

// 便捷构造：编码一段已知指令并返回对象文件（自校验用）
ObjectFile* assemble_ret_nop();

// peephole 优化：扫描代码段，删除连续冗余 nop(0x90) 填充，返回删除字节数。
int peephole_optimize(ObjectFile* obj);

// 统计代码段中指定字节出现次数（自校验/教学用）
int count_byte(ObjectFile* obj, int b);

// 迷你反汇编：从 off 解码一条指令，把助记符写入 out，返回指令长度（字节）。
// 仅支持本汇编器已实现的指令子集，未知指令返回 1 并写入 ".byte 0x.."。
int disasm_one(ObjectFile* obj, int off, String& out);

// 在对象文件最前面写一个最小 64 字节 ELF64 头（教学用），返回头长度。
int emit_elf_header(ObjectFile* obj);

// 转储重定位表到文本（教学用）。
void dump_relocs(ObjectFile* obj, String& out);

// 助记符编号 -> 字符串名。
const char* mnemonic_name(int mn);

// 在对象文件末尾追加 n 个单字节 nop 填充，返回追加后的 code_len。
int emit_nops(ObjectFile* obj, int n);

// 汇编器自测试：返回失败数
int assembler_self_test();

} // namespace compiler
} // namespace nefu
