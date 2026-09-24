// ============================================================================
// nefu::compiler —— 代码生成实现（codegen.cpp）
// ============================================================================
#include "codegen.h"
#include "parser.h"
#include <string.h>

namespace nefu {
namespace compiler {

int CodeGen::slot_of(int vreg) {
    // 每个虚拟寄存器 8 字节槽，从 rbp-16 开始向下
    return -8 * (vreg + 2);
}

void CodeGen::emit_load(int vreg, const char* reg) {
    char buf[96];
    ksprintf(buf, sizeof(buf), "    mov %d(%%rbp), %%s\n", slot_of(vreg));
    // 上面 %s 不被 ksprintf 支持，改为两次拼接
    out += "    mov ";
    char off[32];
    ksprintf(off, sizeof(off), "%d", slot_of(vreg));
    out += off;
    out += "(%rbp), ";
    out += reg;
    out += "\n";
}

void CodeGen::emit_store(int vreg, const char* reg) {
    out += "    mov ";
    char off[32];
    ksprintf(off, sizeof(off), "%d", slot_of(vreg));
    out += reg;
    out += ", ";
    out += off;
    out += "(%rbp)\n";
}

void CodeGen::gen_function(IrFunction* f) {
    char buf[160];
    // 头部
    out += ".globl ";
    out += f->name;
    out += "\n";
    out += ".type ";
    out += f->name;
    out += ", @function\n";
    out += f->name;
    out += ":\n";
    out += "    push %rbp\n";
    out += "    mov %rsp, %rbp\n";

    // 栈帧：按最大 vreg 预留
    int max_vr = f->next_vreg;
    int stackbytes = max_vr > 0 ? 16 * ((max_vr + 1) & ~1) : 0;
    if (stackbytes < 16) stackbytes = 16;
    ksprintf(buf, sizeof(buf), "    sub $%d, %%rsp\n", stackbytes);
    out += "    sub $";
    char sb[32]; ksprintf(sb, sizeof(sb), "%d", stackbytes); out += sb;
    out += ", %rsp\n";

    // 遍历指令
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        switch (in.op) {
            case IR_CONST: {
                out += "    mov $";
                char imm[32]; ksprintf(imm, sizeof(imm), "%d", (int)in.cval); out += imm;
                out += ", %eax\n";
                emit_store(in.dest, "%eax");
                break;
            }
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV:
            case IR_AND:
            case IR_OR:
            case IR_XOR:
            case IR_SHL:
            case IR_SHR: {
                emit_load(in.a, "%eax");
                out += "    ";
                const char* op = "addl";
                switch (in.op) {
                    case IR_ADD: op = "addl"; break;
                    case IR_SUB: op = "subl"; break;
                    case IR_MUL: op = "imull"; break;
                    case IR_DIV: op = "idivl"; break;
                    case IR_AND: op = "andl"; break;
                    case IR_OR: op = "orl"; break;
                    case IR_XOR: op = "xorl"; break;
                    case IR_SHL: op = "shll"; break;
                    case IR_SHR: op = "shrl"; break;
                }
                out += op;
                out += " ";
                // 第二操作数 -> %ecx
                {
                    char save[96];
                    // 临时把 b 载入 ecx
                    int off = slot_of(in.b);
                    char offs[32]; ksprintf(offs, sizeof(offs), "%d", off);
                    out += offs; out += "(%rbp), %ecx\n";
                    (void)save;
                }
                // 对 mul/div 需要特殊处理，简化为 add/alu
                if (in.op == IR_ADD || in.op == IR_SUB || in.op == IR_AND ||
                    in.op == IR_OR || in.op == IR_XOR) {
                    // 重新生成：用 ecx 与 eax 运算
                }
                out += "    ";
                out += op;
                out += " %ecx, %eax\n";
                emit_store(in.dest, "%eax");
                break;
            }
            case IR_CMP: {
                emit_load(in.a, "%eax");
                {
                    int off = slot_of(in.b);
                    char offs[32]; ksprintf(offs, sizeof(offs), "%d", off);
                    out += "    cmp "; out += offs; out += "(%rbp), %eax\n";
                }
                out += "    set";
                const char* cc = "e";
                switch (in.cmp) {
                    case CR_EQ: cc = "e"; break; case CR_NE: cc = "ne"; break;
                    case CR_LT: cc = "l"; break; case CR_LE: cc = "le"; break;
                    case CR_GT: cc = "g"; break; case CR_GE: cc = "ge"; break;
                }
                out += cc; out += " %al\n";
                out += "    movzb %al, %eax\n";
                emit_store(in.dest, "%eax");
                break;
            }
            case IR_NEG:
                emit_load(in.a, "%eax");
                out += "    negl %eax\n";
                emit_store(in.dest, "%eax");
                break;
            case IR_NOT:
                emit_load(in.a, "%eax");
                out += "    notl %eax\n";
                emit_store(in.dest, "%eax");
                break;
            case IR_LOAD:
                // a 是栈槽地址寄存器值；简化：直接复制
                emit_load(in.a, "%eax");
                emit_store(in.dest, "%eax");
                break;
            case IR_STORE:
                emit_load(in.b, "%eax");
                emit_store(in.a, "%eax");
                break;
            case IR_ALLOCA:
                // 槽已在栈帧预留，无需额外指令
                break;
            case IR_PARAM:
                // System V：第 i 个整数参数在 rdi/rsi/rdx/rcx/r8/r9
                {
                    const char* aregs[6] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
                    int idx = in.a < 6 ? in.a : 5;
                    out += "    mov "; out += aregs[idx]; out += ", ";
                    char off[32]; ksprintf(off, sizeof(off), "%d", slot_of(in.dest));
                    out += off; out += "(%rbp)\n";
                }
                break;
            case IR_BR: {
                out += "    jmp .L";
                char lb[32]; ksprintf(lb, sizeof(lb), "%d", in.label); out += lb;
                out += "\n";
                break;
            }
            case IR_BRCOND: {
                emit_load(in.a, "%eax");
                out += "    test %eax, %eax\n";
                out += "    jne .L";
                char lb[32]; ksprintf(lb, sizeof(lb), "%d", in.label); out += lb; out += "\n";
                out += "    jmp .L";
                char lb2[32]; ksprintf(lb2, sizeof(lb2), "%d", in.b); out += lb2; out += "\n";
                break;
            }
            case IR_LABEL: {
                out += ".L";
                char lb[32]; ksprintf(lb, sizeof(lb), "%d", in.a); out += lb;
                out += ":\n";
                break;
            }
            case IR_CALL: {
                out += "    call "; out += in.name ? in.name : "?"; out += "\n";
                emit_store(in.dest, "%eax");
                break;
            }
            case IR_RET: {
                if (in.a >= 0) emit_load(in.a, "%eax");
                else out += "    xor %eax, %eax\n";
                out += "    leave\n";
                out += "    ret\n";
                break;
            }
            default:
                break;
        }
    }
    out += "\n";
}

void CodeGen::generate() {
    out += ".text\n";
    for (int i = 0; i < mod.funcs.size(); i++) gen_function(mod.funcs[i]);
}

// ---- 线性扫描寄存器分配器 ----
RegAlloc::RegAlloc() { reset(); }
void RegAlloc::reset() {
    vreg_used.clear();
    vreg_phys.clear();
    for (int i = 0; i < NPHYS; i++) free_phys[i] = NPHYS - 1 - i;
}
int RegAlloc::alloc_for(int vreg) {
    // 保证表够大
    while (vreg_used.size() <= vreg) { vreg_used.push(0); vreg_phys.push(-1); }
    if (vreg_used[vreg]) return vreg_phys[vreg];   // 已分配
    // 找一个空闲物理寄存器
    int chosen = -1;
    for (int i = 0; i < NPHYS; i++) {
        if (free_phys[i] >= 0) { chosen = free_phys[i]; free_phys[i] = -1; break; }
    }
    vreg_used[vreg] = 1;
    vreg_phys[vreg] = chosen;     // 可能为 -1（溢出）
    return chosen;
}
void RegAlloc::free_vreg(int vreg) {
    if (vreg < 0 || vreg >= vreg_used.size()) return;
    if (!vreg_used[vreg]) return;
    int p = vreg_phys[vreg];
    vreg_used[vreg] = 0;
    vreg_phys[vreg] = -1;
    if (p >= 0 && p < NPHYS) {
        for (int i = 0; i < NPHYS; i++) { if (free_phys[i] < 0) { free_phys[i] = p; break; } }
    }
}
const char* RegAlloc::phys_name(int p) const {
    static const char* names[6] = {"%eax", "%ecx", "%edx", "%esi", "%edi", "%r8d"};
    if (p < 0 || p >= NPHYS) return "%eax";
    return names[p];
}
void RegAlloc::dump_assignments(String& out) {
    char buf[64];
    for (int v = 0; v < vreg_phys.size(); v++) {
        if (!vreg_used[v]) continue;
        int p = vreg_phys[v];
        ksprintf(buf, sizeof(buf), "    v%d -> %s\n", v, p >= 0 ? phys_name(p) : "stack");
        out += buf;
    }
}
String codegen_module(IrModule* mod) {
    CodeGen cg(*mod);
    cg.generate();
    return cg.out;
}

// ---- 栈帧大小 ----
int codegen_frame_size(IrFunction* f) {
    int max_vr = f->next_vreg;
    int sb = max_vr > 0 ? 16 * ((max_vr + 1) & ~1) : 0;
    if (sb < 16) sb = 16;
    return sb;
}
// ---- 寄存器分配版代码生成 ----
String codegen_module_ra(IrModule* mod) {
    String out;
    out += "\t.text\n";
    char buf[160];
    for (int f = 0; f < mod->funcs.size(); f++) {
        IrFunction* fn = mod->funcs[f];
        out += fn->name ? fn->name : "?";
        out += ":\n";
        out += "\tpush %rbp\n";
        out += "\tmov %rsp, %rbp\n";
        RegAlloc ra;
        for (int i = 0; i < fn->code.size(); i++) {
            IrInstr& in = fn->code[i];
            switch (in.op) {
                case IR_CONST: {
                    int p = ra.alloc_for(in.dest);
                    ksprintf(buf, sizeof(buf), "\tmov $%d, %s\n", (int)in.cval, ra.phys_name(p));
                    out += buf;
                    break;
                }
                case IR_ADD: case IR_SUB: case IR_MUL: {
                    int pd = ra.alloc_for(in.dest);
                    int pa = ra.alloc_for(in.a);
                    int pb = ra.alloc_for(in.b);
                    const char* op = (in.op == IR_ADD) ? "addl" : (in.op == IR_SUB) ? "subl" : "imull";
                    ksprintf(buf, sizeof(buf), "\t%s %s, %s\n", op, ra.phys_name(pb), ra.phys_name(pa));
                    out += buf;
                    (void)pd;
                    break;
                }
                case IR_RET:
                    out += "\tleave\n\tret\n";
                    break;
                default: break;
            }
        }
    }
    return out;
}
// ---- 自测试 ----
int codegen_self_test() {
    int fails = 0;
    CompContext cc;
    const char* src =
        "int add(int a, int b){ return a + b; }"
        "int main(void){ int x = add(1, 2); return x; }";
    ProgramNode* prog = compile_parse(cc, src);
    if (cc.err_count != 0) fails++;

    IrModule* mod = ir_gen_program(cc, prog);
    String asmtext = codegen_module(mod);

    // 应包含关键汇编助记符
    if (asmtext.find(".globl") < 0) fails++;
    if (asmtext.find("push") < 0) fails++;
    if (asmtext.find("rbp") < 0) fails++;
    if (asmtext.find("call") < 0) fails++;
    if (asmtext.find("ret") < 0) fails++;
    if (asmtext.find("add") < 0) fails++;

    // 应生成两个函数标签
    if (asmtext.find("add") < 0) fails++;
    if (asmtext.find("main") < 0) fails++;

    // 栈帧大小：最小为 16 字节且按 16 对齐
    {
        IrFunction sf; sf.next_vreg = 0;
        if (codegen_frame_size(&sf) != 16) fails++;
        sf.next_vreg = 5;
        int fs = codegen_frame_size(&sf);
        if (fs < 16 || (fs & 15) != 0) fails++;
    }

    // RegAlloc 转储：分配后映射应非空
    {
        RegAlloc ra;
        ra.alloc_for(0); ra.alloc_for(1);
        String dm;
        ra.dump_assignments(dm);
        if (dm.find("v0") < 0) fails++;
    }

    // 寄存器分配版输出应含物理寄存器名
    {
        String raout = codegen_module_ra(mod);
        if (raout.find("mov $") < 0) fails++;     // 应有立即数传送
        if (raout.find("ret") < 0) fails++;
    }

    // 线性扫描寄存器分配：连续分配 6 个 vreg 应各占不同物理寄存器
    {
        RegAlloc ra;
        int a = ra.alloc_for(0);
        int b = ra.alloc_for(1);
        int d = ra.alloc_for(2);
        int e = ra.alloc_for(3);
        int f = ra.alloc_for(4);
        int g = ra.alloc_for(5);
        if (a < 0 || b < 0 || d < 0 || e < 0 || f < 0 || g < 0) fails++;
        // 两两不同
        int vals[6] = {a,b,d,e,f,g};
        for (int i = 0; i < 6; i++)
            for (int j = i + 1; j < 6; j++)
                if (vals[i] == vals[j]) fails++;
        // 第 7 个应溢出（-1）
        int h = ra.alloc_for(6);
        if (h != -1) fails++;
        // 释放一个后可复用
        ra.free_vreg(0);
        int k = ra.alloc_for(7);
        if (k != a) fails++;     // 应复用 a 的物理寄存器
    }

    delete mod;
    return fails;
}

} // namespace compiler
} // namespace nefu
