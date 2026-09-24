// ============================================================================
// nefu::compiler —— 代码生成（codegen.h）
// ----------------------------------------------------------------------------
// 把三地址 IR 翻译为 x86-64 AT&T 语法汇编文本（System V AMD64 调用约定）。
// 寄存器分配采用简单策略：每个虚拟寄存器对应一个栈槽，计算时借助 rax/rbx
// 等寄存器做临时累加。输出为文本指令流，供窗口展示与后续汇编器/链接器使用。
// ============================================================================
#pragma once

#include "ir.h"

namespace nefu {
namespace compiler {

// ---- 代码生成器 ----
struct CodeGen {
    IrModule& mod;
    String out;             // 生成的汇编文本
    int vreg_base;          // 局部槽起始偏移

    CodeGen(IrModule& m) : mod(m), vreg_base(0) {}

    void generate();                      // 生成整个模块
    void gen_function(IrFunction* f);
    int slot_of(int vreg);                // vreg -> rbp 负偏移
    void emit_load(int vreg, const char* reg);   // mov 槽 -> 寄存器
    void emit_store(int vreg, const char* reg); // mov 寄存器 -> 槽
};

// 便捷入口：IR 模块 -> 汇编文本
String codegen_module(IrModule* mod);

// 计算函数栈帧大小（按 vreg 数量估算，16 字节对齐）。
int codegen_frame_size(IrFunction* f);

// 寄存器分配版：用线性扫描寄存器分配器给算术指令分配物理寄存器，
// 输出带物理寄存器名的汇编文本（教学演示寄存器分配效果）。
String codegen_module_ra(IrModule* mod);

// ---- 线性扫描寄存器分配器 ----
// 把虚拟寄存器映射到有限的物理寄存器池。遇到活跃区间重叠时为后到者分配新
// 物理寄存器，池子耗尽则记为溢出（spill 到栈槽）。供 codegen 做寄存器分配。
struct RegAlloc {
    static const int NPHYS = 6;        // 可分配物理寄存器数
    List<int> vreg_phys;               // vreg -> 物理寄存器号（-1 = 溢出）
    List<int> vreg_used;               // 每个 vreg 是否已分配
    int free_phys[NPHYS];             // 空闲物理寄存器栈

    RegAlloc();
    void reset();
    int alloc_for(int vreg);           // 为 vreg 分配物理寄存器（或 -1 溢出）
    void free_vreg(int vreg);          // 结束其活跃区间，回收物理寄存器
    const char* phys_name(int p) const;
    void dump_assignments(String& out);   // 打印 vreg->物理寄存器映射
};

// codegen 自测试：返回失败数
int codegen_self_test();

} // namespace compiler
} // namespace nefu
