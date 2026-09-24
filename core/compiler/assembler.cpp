// ============================================================================
// nefu::compiler —— 迷你 x86-64 汇编器实现（assembler.cpp）
// ============================================================================
#include "assembler.h"
#include "../platform.h"
#include <string.h>

namespace nefu {
namespace compiler {

// ---- ObjectFile ----
ObjectFile::ObjectFile() : code(0), code_len(0), code_cap(0), data(0), data_len(0), data_cap(0) {}
ObjectFile::~ObjectFile() {
    if (code) kfree(code);
    code = 0;
    if (data) kfree(data);
    data = 0;
}
void ObjectFile::emit_byte(int b) {
    if (code_len + 1 > code_cap) {
        int nc = code_cap ? code_cap * 2 : 256;
        uint8_t* na = (uint8_t*)kalloc(nc);
        for (int i = 0; i < code_len; i++) na[i] = code[i];
        if (code) kfree(code);
        code = na; code_cap = nc;
    }
    code[code_len++] = (uint8_t)b;
}
void ObjectFile::emit32(int32_t v) {
    emit_byte(v & 0xff); emit_byte((v >> 8) & 0xff);
    emit_byte((v >> 16) & 0xff); emit_byte((v >> 24) & 0xff);
}
void ObjectFile::emit64(int64_t v) {
    for (int i = 0; i < 8; i++) emit_byte((v >> (8 * i)) & 0xff);
}
// .data 段写入
void ObjectFile::emit_data_byte(int b) {
    if (data_len + 1 > data_cap) {
        int nc = data_cap ? data_cap * 2 : 64;
        uint8_t* na = (uint8_t*)kalloc(nc);
        for (int i = 0; i < data_len; i++) na[i] = data[i];
        if (data) kfree(data);
        data = na; data_cap = nc;
    }
    data[data_len++] = (uint8_t)b;
}
int ObjectFile::data_checksum() {
    int s = 0;
    for (int i = 0; i < data_len; i++) s += data[i];
    return s;
}

// ---- 单条指令长度 ----
int Assembler::instr_size(const AsmInstr& in) {
    switch (in.mn) {
        case ASM_NOP: return 1;
        case ASM_RET: return 1;
        case ASM_LEAVE: return 1;
        case ASM_PUSH_RBP: return 1;
        case ASM_POP_RBP: return 1;
        case ASM_MOV_RBP_RSP: return 3;
        case ASM_SUB_RSP_IMM: return 7;     // REX + 81 EC + imm32
        case ASM_MOV_REG_IMM: return 5;    // B8+rd + imm32
        case ASM_MOV_REG_STACK: return 5;  // REX + 8B + ModRM + disp8
        case ASM_MOV_STACK_REG: return 5;
        case ASM_ADD_REG: return 2;
        case ASM_SUB_REG: return 2;
        case ASM_IMUL_REG: return 4;
        case ASM_CMP_STACK: return 5;
        case ASM_CALL: return 5;           // E8 + rel32
        case ASM_JMP_LABEL: return 5;     // E9 + rel32
        case ASM_JCC_LABEL: return 6;     // 0F 8x + rel32
        case ASM_MOV_REG_REG: return 3;    // REX 89 /r
        case ASM_LEA: return 5;           // REX 8D /r disp8
        case ASM_ADD_REG_IMM: return 4;   // 83 /0 ib
        case ASM_SUB_REG_IMM: return 4;   // 83 /5 ib
        case ASM_PUSH_REG: return 1;      // 50+r
        case ASM_POP_REG: return 1;       // 58+r
        case ASM_TEST_REG: return 2;      // 85 /r
        case ASM_NOP_WORD: return 2;      // 66 90
        case ASM_GLOB: return 0;
        case ASM_LABEL: return 0;
    }
    return 1;
}

void Assembler::add(const AsmInstr& in) {
    instrs.push(in);
}

void Assembler::assemble() {
    // 第一遍：计算标签地址
    int addr = 0;
    for (int i = 0; i < instrs.size(); i++) {
        const AsmInstr& in = instrs[i];
        if (in.mn == ASM_LABEL) {
            while (label_addr.size() <= in.label) label_addr.push(0);
            label_addr[in.label] = addr;
        } else {
            addr += instr_size(in);
        }
    }
    // 第二遍：编码
    for (int i = 0; i < instrs.size(); i++) {
        const AsmInstr& in = instrs[i];
        int here = obj.code_len;
        switch (in.mn) {
            case ASM_NOP: obj.emit_byte(0x90); break;
            case ASM_RET: obj.emit_byte(0xC3); break;
            case ASM_LEAVE: obj.emit_byte(0xC9); break;
            case ASM_PUSH_RBP: obj.emit_byte(0x55); break;
            case ASM_POP_RBP: obj.emit_byte(0x5D); break;
            case ASM_MOV_RBP_RSP:
                obj.emit_byte(0x48); obj.emit_byte(0x89); obj.emit_byte(0xE5);
                break;
            case ASM_SUB_RSP_IMM:
                obj.emit_byte(0x48); obj.emit_byte(0x81); obj.emit_byte(0xEC);
                obj.emit32((int32_t)in.imm);
                break;
            case ASM_MOV_REG_IMM:
                obj.emit_byte(0xB8 + (in.reg & 7));
                obj.emit32((int32_t)in.imm);
                break;
            case ASM_MOV_REG_STACK:   // mov disp(%rbp), %reg
                obj.emit_byte(0x48);
                obj.emit_byte(0x8B);
                obj.emit_byte(0x45 | ((in.reg & 7) << 3));   // mod=01 reg rm=101
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_MOV_STACK_REG:    // mov %reg, disp(%rbp)
                obj.emit_byte(0x48);
                obj.emit_byte(0x89);
                obj.emit_byte(0x45 | ((in.reg & 7) << 3));
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_ADD_REG:
                obj.emit_byte(0x01);
                obj.emit_byte(0xC0 | (in.reg & 7));
                break;
            case ASM_SUB_REG:
                obj.emit_byte(0x29);
                obj.emit_byte(0xC0 | (in.reg & 7));
                break;
            case ASM_IMUL_REG:
                obj.emit_byte(0x0F); obj.emit_byte(0xAF);
                obj.emit_byte(0xC0 | (in.reg & 7));
                break;
            case ASM_CMP_STACK:
                obj.emit_byte(0x48); obj.emit_byte(0x3B);
                obj.emit_byte(0x45);
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_CALL: {
                obj.emit_byte(0xE8);
                obj.emit32(0);   // 占位，重定位补
                Reloc r; r.offset = here + 1; r.sym = in.sym; r.addend = 0;
                obj.relocs.push(r);
                break;
            }
            case ASM_JMP_LABEL: {
                obj.emit_byte(0xE9);
                int target = in.label < label_addr.size() ? label_addr[in.label] : 0;
                int32_t rel = target - (here + 5);
                obj.emit32(rel);
                break;
            }
            case ASM_JCC_LABEL: {
                static const uint8_t opc[6] = {0x84, 0x85, 0x8C, 0x8E, 0x8F, 0x8D};
                obj.emit_byte(0x0F);
                obj.emit_byte(opc[in.cc < 6 ? in.cc : 0]);
                int target = in.label < label_addr.size() ? label_addr[in.label] : 0;
                int32_t rel = target - (here + 6);
                obj.emit32(rel);
                break;
            }
            case ASM_GLOB: {
                Symbol s; s.name = in.sym; s.offset = here; s.kind = 0;
                obj.syms.push(s);
                break;
            }
            case ASM_MOV_REG_REG:   // mov %reg, %reg2  (ModRM: reg=reg2, rm=reg)
                obj.emit_byte(0x48);
                obj.emit_byte(0x89);
                obj.emit_byte(0xC0 | ((in.reg & 7) << 3) | (in.imm & 7));
                break;
            case ASM_LEA:           // lea disp(%rbp), %reg
                obj.emit_byte(0x48);
                obj.emit_byte(0x8D);
                obj.emit_byte(0x45 | ((in.reg & 7) << 3));
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_ADD_REG_IMM:   // add $imm8, %reg
                obj.emit_byte(0x83);
                obj.emit_byte(0xC0 | (in.reg & 7));
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_SUB_REG_IMM:   // sub $imm8, %reg
                obj.emit_byte(0x83);
                obj.emit_byte(0xE8 | (in.reg & 7));
                obj.emit_byte((int8_t)in.imm);
                break;
            case ASM_PUSH_REG:
                obj.emit_byte(0x50 | (in.reg & 7));
                break;
            case ASM_POP_REG:
                obj.emit_byte(0x58 | (in.reg & 7));
                break;
            case ASM_TEST_REG:     // test %reg, %reg
                obj.emit_byte(0x85);
                obj.emit_byte(0xC0 | (in.reg & 7));
                break;
            case ASM_NOP_WORD:
                obj.emit_byte(0x66); obj.emit_byte(0x90);
                break;
            case ASM_LABEL: break;
        }
    }
}

ObjectFile* assemble_ret_nop() {
    Assembler a;
    AsmInstr i1; memset(&i1, 0, sizeof(i1)); i1.mn = ASM_PUSH_RBP; a.add(i1);
    AsmInstr i2; memset(&i2, 0, sizeof(i2)); i2.mn = ASM_MOV_RBP_RSP; a.add(i2);
    AsmInstr i3; memset(&i3, 0, sizeof(i3)); i3.mn = ASM_NOP; a.add(i3);
    AsmInstr i4; memset(&i4, 0, sizeof(i4)); i4.mn = ASM_POP_RBP; a.add(i4);
    AsmInstr i5; memset(&i5, 0, sizeof(i5)); i5.mn = ASM_RET; a.add(i5);
    a.assemble();
    ObjectFile* of = new ObjectFile();
    // 拷贝出结果（assembler.obj 是局部的，需要拷到新堆）
    for (int i = 0; i < a.obj.code_len; i++) of->emit_byte(a.obj.code[i]);
    return of;
}

// ---- peephole 优化 ----
int count_byte(ObjectFile* obj, int b) {
    int n = 0;
    for (int i = 0; i < obj->code_len; i++)
        if (obj->code[i] == (uint8_t)b) n++;
    return n;
}
int peephole_optimize(ObjectFile* obj) {
    if (obj->code_len <= 1) return 0;
    int removed = 0;
    // 重建：连续两个以上 0x90 nop 压缩为一个
    uint8_t* out = (uint8_t*)kalloc(obj->code_cap > 0 ? obj->code_cap : 64);
    int n = 0;
    int i = 0;
    while (i < obj->code_len) {
        if (obj->code[i] == 0x90) {
            // 统计连续 nop
            int run = 0;
            while (i < obj->code_len && obj->code[i] == 0x90) { run++; i++; }
            out[n++] = 0x90;             // 保留一个
            removed += run - 1;
        } else {
            out[n++] = obj->code[i++];
        }
    }
    // 回写
    for (int k = 0; k < n; k++) obj->code[k] = out[k];
    obj->code_len = n;
    kfree(out);
    return removed;
}
// ---- nop 填充 ----
int emit_nops(ObjectFile* obj, int n) {
    for (int i = 0; i < n; i++) obj->emit_byte(0x90);
    return obj->code_len;
}
// ---- 助记符名表 ----
const char* mnemonic_name(int mn) {
    switch (mn) {
        case ASM_NOP: return "nop";
        case ASM_RET: return "ret";
        case ASM_PUSH_RBP: return "push_rbp";
        case ASM_POP_RBP: return "pop_rbp";
        case ASM_MOV_RBP_RSP: return "mov_rbp_rsp";
        case ASM_SUB_RSP_IMM: return "sub_rsp_imm";
        case ASM_MOV_REG_IMM: return "mov_reg_imm";
        case ASM_MOV_REG_STACK: return "mov_reg_stack";
        case ASM_MOV_STACK_REG: return "mov_stack_reg";
        case ASM_ADD_REG: return "add_reg";
        case ASM_SUB_REG: return "sub_reg";
        case ASM_IMUL_REG: return "imul_reg";
        case ASM_CMP_STACK: return "cmp_stack";
        case ASM_CALL: return "call";
        case ASM_JMP_LABEL: return "jmp";
        case ASM_JCC_LABEL: return "jcc";
        case ASM_LEAVE: return "leave";
        case ASM_GLOB: return ".globl";
        case ASM_LABEL: return ".label";
        case ASM_MOV_REG_REG: return "mov_reg_reg";
        case ASM_LEA: return "lea";
        case ASM_ADD_REG_IMM: return "add_reg_imm";
        case ASM_SUB_REG_IMM: return "sub_reg_imm";
        case ASM_PUSH_REG: return "push_reg";
        case ASM_POP_REG: return "pop_reg";
        case ASM_TEST_REG: return "test_reg";
        case ASM_NOP_WORD: return "nop_word";
        default: return "?";
    }
}
// ---- 迷你反汇编 ----
static const char* reg_name8(int r) {
    static const char* nm[8] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
    return nm[r & 7];
}
int disasm_one(ObjectFile* obj, int off, String& out) {
    if (off < 0 || off >= obj->code_len) { out += "(eof)"; return 0; }
    char buf[96];
    uint8_t* d = obj->code;
    int b = d[off];
    if (b == 0x90) { out += "nop"; return 1; }
    if (b == 0xC3) { out += "ret"; return 1; }
    if (b == 0xC9) { out += "leave"; return 1; }
    if (b == 0x55) { out += "push %rbp"; return 1; }
    if (b == 0x5D) { out += "pop %rbp"; return 1; }
    if (b >= 0x50 && b <= 0x57) { ksprintf(buf, sizeof(buf), "push %s", reg_name8(b - 0x50)); out += buf; return 1; }
    if (b >= 0x58 && b <= 0x5F) { ksprintf(buf, sizeof(buf), "pop %s", reg_name8(b - 0x58)); out += buf; return 1; }
    if (b == 0xB8 || b == 0xB9 || b == 0xBA || b == 0xBB) {
        int32_t imm = 0;
        if (off + 5 <= obj->code_len)
            imm = d[off+1] | (d[off+2]<<8) | (d[off+3]<<16) | (d[off+4]<<24);
        ksprintf(buf, sizeof(buf), "mov $%d, %s", (int)imm, reg_name8(b - 0xB8));
        out += buf; return 5;
    }
    if (b == 0xE8 || b == 0xE9) {
        int32_t rel = 0;
        if (off + 5 <= obj->code_len)
            rel = d[off+1] | (d[off+2]<<8) | (d[off+3]<<16) | (d[off+4]<<24);
        ksprintf(buf, sizeof(buf), "%s %d", b == 0xE8 ? "call" : "jmp", (int)rel);
        out += buf; return 5;
    }
    // 未知指令：按字节打印
    ksprintf(buf, sizeof(buf), ".byte 0x%02x", b);
    out += buf;
    return 1;
}
// ---- 最小 ELF64 头 ----
int emit_elf_header(ObjectFile* obj) {
    // 先把现有代码保存到临时缓冲
    int oldlen = obj->code_len;
    uint8_t* saved = 0;
    if (oldlen > 0) {
        saved = (uint8_t*)kalloc(oldlen);
        for (int i = 0; i < oldlen; i++) saved[i] = obj->code[i];
    }
    obj->code_len = 0;
    // e_ident
    obj->emit_byte(0x7F); obj->emit_byte('E'); obj->emit_byte('L'); obj->emit_byte('F');
    obj->emit_byte(2);     // EI_CLASS = ELFCLASS64
    obj->emit_byte(1);     // EI_DATA = little endian
    obj->emit_byte(1);     // EI_VERSION
    obj->emit_byte(0); obj->emit_byte(0); obj->emit_byte(0);
    obj->emit_byte(0); obj->emit_byte(0); obj->emit_byte(0); obj->emit_byte(0);
    obj->emit_byte(0); obj->emit_byte(0);
    // e_type(2) / e_machine(2)
    obj->emit_byte(2); obj->emit_byte(0);   // e_type=EXEC
    obj->emit_byte(62); obj->emit_byte(0);  // e_machine=x86-64
    // 其余字段填零到 64 字节
    while (obj->code_len < 64) obj->emit_byte(0);
    // 拼回原代码
    for (int i = 0; i < oldlen; i++) obj->emit_byte(saved[i]);
    if (saved) kfree(saved);
    return 64;
}
// ---- 重定位转储 ----
void dump_relocs(ObjectFile* obj, String& out) {
    char buf[96];
    for (int i = 0; i < obj->relocs.size(); i++) {
        Reloc& r = obj->relocs[i];
        ksprintf(buf, sizeof(buf), "  reloc @%d -> %s\n", r.offset, r.sym ? r.sym : "?");
        out += buf;
    }
    if (obj->relocs.size() == 0) out += "  (none)\n";
}
// ---- 自测试 ----
int assembler_self_test() {
    int fails = 0;
    ObjectFile* of = assemble_ret_nop();

    // 期望字节序列：push rbp(0x55) mov rbp,rsp(48 89 E5) nop(90) pop rbp(5D) ret(C3)
    if (of->code_len != 7) fails++;
    if (of->code_len >= 1 && of->code[0] != 0x55) fails++;         // push rbp
    if (of->code_len >= 4 && of->code[1] != 0x48) fails++;         // REX.W
    if (of->code_len >= 4 && of->code[2] != 0x89) fails++;          // mov
    if (of->code_len >= 4 && of->code[3] != 0xE5) fails++;          // ModR/M
    if (of->code_len >= 5 && of->code[4] != 0x90) fails++;         // nop
    if (of->code_len >= 6 && of->code[5] != 0x5D) fails++;         // pop rbp
    if (of->code_len >= 7 && of->code[6] != 0xC3) fails++;         // ret

    // 单独验证 call / jmp 重定位记录
    Assembler a;
    AsmInstr g; memset(&g, 0, sizeof(g)); g.mn = ASM_GLOB; g.sym = "foo"; a.add(g);
    AsmInstr l0; memset(&l0, 0, sizeof(l0)); l0.mn = ASM_LABEL; l0.label = 0; a.add(l0);
    AsmInstr c; memset(&c, 0, sizeof(c)); c.mn = ASM_CALL; c.sym = "printf"; a.add(c);
    AsmInstr j; memset(&j, 0, sizeof(j)); j.mn = ASM_JMP_LABEL; j.label = 0; a.add(j);
    AsmInstr r; memset(&r, 0, sizeof(r)); r.mn = ASM_RET; a.add(r);
    a.assemble();
    if (a.obj.relocs.size() != 1) fails++;           // call 产生一条重定位
    if (a.obj.syms.size() != 1) fails++;            // glob 产生一个符号

    // 新指令编码：push rax(0x50) / add $3,eax(83 c0 03) / pop rdx(0x5a)
    {
        Assembler b;
        AsmInstr p; memset(&p, 0, sizeof(p)); p.mn = ASM_PUSH_REG; p.reg = REG_RAX; b.add(p);
        AsmInstr ad; memset(&ad, 0, sizeof(ad)); ad.mn = ASM_ADD_REG_IMM; ad.reg = REG_RAX; ad.imm = 3; b.add(ad);
        AsmInstr po; memset(&po, 0, sizeof(po)); po.mn = ASM_POP_REG; po.reg = REG_RDX; b.add(po);
        b.assemble();
        if (b.obj.code_len != 5) fails++;                       // push(1)+addimm(3)+pop(1)
        if (b.obj.code[0] != 0x50) fails++;                    // push rax
        if (b.obj.code[1] != 0x83) fails++;                     // add /
        if (b.obj.code[2] != 0xC0) fails++;                     // modrm eax
        if (b.obj.code[3] != 0x03) fails++;                   // imm 3
        if (b.obj.code[4] != 0x5A) fails++;                     // pop rdx
    }

    // mov reg->reg 编码：mov %eax,%ecx = 48 89 c1
    {
        Assembler b;
        AsmInstr m; memset(&m, 0, sizeof(m)); m.mn = ASM_MOV_REG_REG; m.reg = REG_RCX; m.imm = REG_RAX; b.add(m);
        b.assemble();
        if (b.obj.code_len != 3) fails++;
        if (b.obj.code[0] != 0x48) fails++;
        if (b.obj.code[1] != 0x89) fails++;
        if (b.obj.code[2] != 0xC8) fails++;   // ModRM reg=RCX rm=RAX
    }

    // peephole：人工构造连续 nop，压缩后应减少 nop 总数
    {
        ObjectFile* o = new ObjectFile();
        uint8_t body[6] = { 0x90, 0x90, 0x90, 0xC3, 0x90, 0x90 };
        for (int i = 0; i < 6; i++) o->emit_byte(body[i]);
        int before = count_byte(o, 0x90);
        int rem = peephole_optimize(o);
        int after = count_byte(o, 0x90);
        if (before != 5) fails++;
        if (rem != 3) fails++;
        if (after != 2) fails++;
        delete o;
    }

    // nop 填充：追加 4 个 nop，长度应增加 4
    {
        ObjectFile* n = new ObjectFile();
        n->emit_byte(0xC3);
        int after = emit_nops(n, 4);
        if (after != 5) fails++;
        if (count_byte(n, 0x90) != 4) fails++;
        delete n;
    }

    // .data 段：写入字节后 checksum 正确
    {
        ObjectFile* d = new ObjectFile();
        d->emit_data_byte(1); d->emit_data_byte(2); d->emit_data_byte(3);
        if (d->data_len != 3) fails++;
        if (d->data_checksum() != 6) fails++;
        delete d;
    }

    // 助记符名表：关键指令名非空且正确
    {
        if (mnemonic_name(ASM_RET)[0] != 'r') fails++;       // ret
        if (mnemonic_name(ASM_NOP)[0] != 'n') fails++;
        if (strcmp(mnemonic_name(ASM_CALL), "call") != 0) fails++;
    }

    // 重定位转储：带 call 的对象应有 reloc 条目
    {
        Assembler ra;
        AsmInstr g; memset(&g,0,sizeof(g)); g.mn=ASM_GLOB; g.sym="foo"; ra.add(g);
        AsmInstr ca; memset(&ca,0,sizeof(ca)); ca.mn=ASM_CALL; ca.sym="printf"; ra.add(ca);
        ra.assemble();
        String rd;
        dump_relocs(&ra.obj, rd);
        if (rd.find("printf") < 0) fails++;
    }

    // 最小 ELF 头：前四字节应为 \x7fELF
    {
        ObjectFile* e = new ObjectFile();
        e->emit_byte(0xC3);   // ret
        int hlen = emit_elf_header(e);
        if (hlen != 64) fails++;
        if (e->code[0] != 0x7F || e->code[1] != 'E' || e->code[2] != 'L' || e->code[3] != 'F') fails++;
        if (e->code_len != 65) fails++;      // 64 头 + 1 字节 ret
        delete e;
    }

    // 迷你反汇编：push rbp / nop / ret
    {
        ObjectFile* o = new ObjectFile();
        o->emit_byte(0x55);   // push rbp
        o->emit_byte(0x90);   // nop
        o->emit_byte(0xC3);    // ret
        String d0, d1, d2;
        int n0 = disasm_one(o, 0, d0);
        int n1 = disasm_one(o, n0, d1);
        disasm_one(o, n0 + n1, d2);
        if (d0.find("push") < 0 || d0.find("rbp") < 0) fails++;
        if (d1.find("nop") < 0) fails++;
        if (d2.find("ret") < 0) fails++;
        if (n0 != 1 || n1 != 1) fails++;
        delete o;
    }

    delete of;
    return fails;
}

} // namespace compiler
} // namespace nefu
