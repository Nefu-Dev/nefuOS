// ============================================================================
// nefu::compiler —— 迷你链接器实现（linker.cpp）
// ============================================================================
#include "linker.h"
#include "../platform.h"
#include <string.h>

namespace nefu {
namespace compiler {

Executable::~Executable() {
    if (text) kfree(text);
    text = 0;
}

void Linker::add(ObjectFile* o) {
    objs.push(o);
}

Executable* Linker::link() {
    Executable* ex = new Executable();
    ex->text = 0; ex->text_len = 0; ex->entry = 0;

    // 计算总长度
    int total = 0;
    for (int i = 0; i < objs.size(); i++) total += objs[i]->code_len;
    if (total <= 0) total = 1;
    ex->text = (uint8_t*)kalloc(total);
    ex->text_len = total;

    // 第一遍：拼接代码 + 登记符号
    int cursor = 0;
    for (int i = 0; i < objs.size(); i++) {
        ObjectFile* o = objs[i];
        for (int j = 0; j < o->code_len; j++) ex->text[cursor + j] = o->code[j];
        for (int s = 0; s < o->syms.size(); s++) {
            const char* nm = o->syms[s].name;
            int addr = cursor + o->syms[s].offset;
            // 已定义？
            bool dup = false;
            for (int k = 0; k < sym_names.size(); k++) {
                if (strcmp(sym_names[k], nm) == 0) { dup = true; break; }
            }
            if (!dup) {
                sym_names.push(nm);
                sym_addrs.push(addr);
                sym_objidx.push(i);
            }
            if (strcmp(nm, "main") == 0) ex->entry = addr;
        }
        cursor += o->code_len;
    }

    // 第二遍：应用重定位
    cursor = 0;
    for (int i = 0; i < objs.size(); i++) {
        ObjectFile* o = objs[i];
        for (int r = 0; r < o->relocs.size(); r++) {
            Reloc& rl = o->relocs[r];
            int addr = -1;
            for (int k = 0; k < sym_names.size(); k++) {
                if (strcmp(sym_names[k], rl.sym) == 0) { addr = sym_addrs[k]; break; }
            }
            if (addr < 0) {
                ex->undefined.push(rl.sym);
            } else {
                // 回填相对偏移（call rel32）
                int site = cursor + rl.offset;
                int32_t rel = addr - (site + 4);
                ex->text[site]     = (uint8_t)(rel & 0xff);
                ex->text[site + 1] = (uint8_t)((rel >> 8) & 0xff);
                ex->text[site + 2] = (uint8_t)((rel >> 16) & 0xff);
                ex->text[site + 3] = (uint8_t)((rel >> 24) & 0xff);
            }
        }
        cursor += o->code_len;
    }

    // 日志
    char buf[128];
    ksprintf(buf, sizeof(buf), "链接完成：%d 个目标，代码段 %d 字节，定义符号 %d，未定义 %d\n",
             objs.size(), ex->text_len, sym_names.size(), ex->undefined.size());
    ex->log += buf;
    return ex;
}

// ---- 符号查询与转储 ----
int Linker::resolve(const char* sym) {
    for (int k = 0; k < sym_names.size(); k++)
        if (strcmp(sym_names[k], sym) == 0) return sym_addrs[k];
    return -1;
}
void Linker::dump_symbols(String& out) {
    char buf[128];
    for (int k = 0; k < sym_names.size(); k++) {
        ksprintf(buf, sizeof(buf), "  %08x  %s\n", sym_addrs[k], sym_names[k]);
        out += buf;
    }
}
void Linker::emit_map(String& out) {
    out += "===== symbol map =====\n";
    char buf[96];
    for (int i = 0; i < sym_names.size(); i++) {
        ksprintf(buf, sizeof(buf), "  %08x  %s\n", (int)sym_addrs[i], sym_names[i]);
        out += buf;
    }
    out += "  (共 "; char nb[16]; ksprintf(nb, sizeof(nb), "%d", sym_names.size()); out += nb; out += " 个符号)\n";
}
// ---- 自测试 ----
int linker_self_test() {
    int fails = 0;

    // 目标 1：定义 main（push/mov/nop/ret）
    Assembler a1;
    AsmInstr g; memset(&g, 0, sizeof(g)); g.mn = ASM_GLOB; g.sym = "main"; a1.add(g);
    AsmInstr p; memset(&p, 0, sizeof(p)); p.mn = ASM_PUSH_RBP; a1.add(p);
    AsmInstr c; memset(&c, 0, sizeof(c)); c.mn = ASM_CALL; c.sym = "printf"; a1.add(c);
    AsmInstr r; memset(&r, 0, sizeof(r)); r.mn = ASM_RET; a1.add(r);
    a1.assemble();

    // 目标 2：空对象，只有一个 ret
    Assembler a2;
    AsmInstr g2; memset(&g2, 0, sizeof(g2)); g2.mn = ASM_GLOB; g2.sym = "helper"; a2.add(g2);
    AsmInstr r2; memset(&r2, 0, sizeof(r2)); r2.mn = ASM_RET; a2.add(r2);
    a2.assemble();

    Linker lk;
    lk.add(&a1.obj);
    lk.add(&a2.obj);
    Executable* ex = lk.link();

    // 两个定义符号 main / helper
    if (lk.sym_names.size() != 2) fails++;
    // printf 未定义
    if (ex->undefined.size() != 1) fails++;
    // 入口点应指向 main
    if (ex->entry < 0 || ex->entry >= ex->text_len) fails++;
    // 代码段长度应为两个对象之和
    if (ex->text_len != a1.obj.code_len + a2.obj.code_len) fails++;

    // 符号解析：main / helper 应可解析，不存在符号返回 -1
    if (lk.resolve("main") < 0) fails++;
    if (lk.resolve("helper") < 0) fails++;
    if (lk.resolve("no_such_sym") != -1) fails++;

    // 符号表转储应含两个符号名
    String symdump;
    lk.dump_symbols(symdump);
    if (symdump.find("main") < 0) fails++;
    if (symdump.find("helper") < 0) fails++;

    delete ex;

    // 重定义符号去重：两个对象都定义 dup，符号表应只记一次
    {
        Assembler d1;
        AsmInstr g1; memset(&g1,0,sizeof(g1)); g1.mn=ASM_GLOB; g1.sym="dup"; d1.add(g1);
        AsmInstr r1; memset(&r1,0,sizeof(r1)); r1.mn=ASM_RET; d1.add(r1); d1.assemble();
        Assembler d2;
        AsmInstr g2; memset(&g2,0,sizeof(g2)); g2.mn=ASM_GLOB; g2.sym="dup"; d2.add(g2);
        AsmInstr r2; memset(&r2,0,sizeof(r2)); r2.mn=ASM_RET; d2.add(r2); d2.assemble();
        Linker lk2;
        lk2.add(&d1.obj); lk2.add(&d2.obj);
        Executable* ex2 = lk2.link();
        int dupcount = 0;
        for (int k = 0; k < lk2.sym_names.size(); k++)
            if (strcmp(lk2.sym_names[k], "dup") == 0) dupcount++;
        if (dupcount != 1) fails++;       // 重定义应被去重
        delete ex2;
    }

    return fails;
}

} // namespace compiler
} // namespace nefu
