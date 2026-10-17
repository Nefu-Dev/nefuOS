// wasm_min.h — minimal WebAssembly MVP interpreter for the mini browser.
// Lays the runtime foundation so modules such as Draco's decoder can later be
// loaded. Supports: linear memory (with cross-call persistence via Instance),
// globals/locals, i32/i64/f32/f64, calls, call_indirect, function imports,
// core arithmetic, memory ops, and *real structured control flow*
// (block/loop/if/else/end with proper branch targets; br/br_if/br_table).
// Software (CPU-only) VM; self-contained, no external deps.
//
// Use:
//   parse(bytes,len,&module)                    -> 0 ok
//   run_module(module,"name",args,&out)         -> stateless one-shot call
//   Instance inst; parse(&inst.m); inst.init();
//   run_instance(inst,"name",args,&out)         -> stateful (memory persists)
#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <utility>
#include <unordered_map>

namespace nefu_wasm {

enum { T_I32 = 0x7F, T_I64 = 0x7E, T_F32 = 0x7D, T_F64 = 0x7C };

struct FuncType { std::vector<uint8_t> params, results; };

struct Func {
    uint32_t type;
    std::vector<uint8_t> local_types;
    uint32_t local_count = 0;
    std::vector<uint8_t> body;
};

struct Global { uint8_t type; bool mut; uint64_t init; };

// function import (kind 0); referenced in the func index space before defined funcs
struct ImportFunc { std::string module, name; uint32_t type; };
struct ImportGlobal { std::string module, name; uint8_t type; bool mut; };

struct Module {
    std::vector<FuncType> types;
    std::vector<uint32_t> func_type;                // defined funcs only
    std::vector<Func> funcs;                        // defined funcs only
    std::vector<Global> globals;                    // defined globals only
    std::vector<std::pair<std::string, std::pair<uint8_t, uint32_t>>> exports;
    uint8_t* mem = nullptr; uint32_t mem_size = 0; uint32_t mem_max = 0;
    bool has_mem = false;                           // defined memory present
    std::vector<uint32_t> table; bool has_table = false;
    uint32_t start_idx = 0xFFFFFFFFu;               // full-space func index, if any

    std::vector<ImportFunc> import_funcs;
    std::vector<ImportGlobal> import_globals;
    ImportGlobal import_mem; bool has_import_mem = false;
    ImportGlobal import_table; bool has_import_table = false;

    uint32_t func_count() const { return (uint32_t)(import_funcs.size() + funcs.size()); }
    uint32_t global_count() const { return (uint32_t)(import_globals.size() + globals.size()); }
    ~Module(){ if (mem) free(mem); }
};

static uint32_t rd_leb(const uint8_t*& p, const uint8_t* end, bool& ok) {
    uint32_t v = 0; int shift = 0;
    while (p < end && shift < 35) {
        uint8_t b = *p++;
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { ok = true; return v; }
        shift += 7;
    }
    ok = false; return 0;
}
static int64_t rd_sleb(const uint8_t*& p, const uint8_t* end, bool& ok) {
    uint64_t v = 0; int shift = 0; uint8_t byte;
    do {
        if (p >= end || shift >= 64) { ok = false; return 0; }
        byte = *p++;
        v |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 64 && (byte & 0x40)) v |= (~0ULL << shift);
    ok = true; return (int64_t)v;
}
// index-based readers for the pc-driven interpreter (avoids binding an rvalue
// pointer to the const-ref readers above)
static uint32_t rdleb_i(const uint8_t* body, uint32_t& pc, uint32_t blen, bool& ok) {
    uint32_t v = 0; int shift = 0;
    while (pc < blen && shift < 35) {
        uint8_t b = body[pc++];
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { ok = true; return v; }
        shift += 7;
    }
    ok = false; return 0;
}
static int64_t rdsleb_i(const uint8_t* body, uint32_t& pc, uint32_t blen, bool& ok) {
    uint64_t v = 0; int shift = 0; uint8_t byte;
    do {
        if (pc >= blen || shift >= 64) { ok = false; return 0; }
        byte = body[pc++];
        v |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 64 && (byte & 0x40)) v |= (~0ULL << shift);
    ok = true; return (int64_t)v;
}
static double rdf32_i(const uint8_t* body, uint32_t& pc, uint32_t blen, bool& ok) {
    if (pc + 4 > blen) { ok = false; return 0; }
    uint32_t v; memcpy(&v, body + pc, 4); pc += 4; ok = true;
    float f; memcpy(&f, &v, 4); return f;
}
static double rdf64_i(const uint8_t* body, uint32_t& pc, uint32_t blen, bool& ok) {
    if (pc + 8 > blen) { ok = false; return 0; }
    double d; memcpy(&d, body + pc, 8); pc += 8; ok = true; return d;
}
static double rd_f32(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 4 > end) { ok = false; return 0; }
    uint32_t v; memcpy(&v, p, 4); p += 4; ok = true;
    float f; memcpy(&f, &v, 4); return f;
}
static double rd_f64(const uint8_t*& p, const uint8_t* end, bool& ok) {
    if (p + 8 > end) { ok = false; return 0; }
    double d; memcpy(&d, p, 8); p += 8; ok = true; return d;
}

// ---- structured control-flow block table ----
struct BlockInfo {
    uint8_t kind = 0;          // 0x02 block, 0x03 loop, 0x04 if
    uint32_t end = 0;          // offset of the matching `end` opcode byte
    uint32_t loop_start = 0;   // offset of first body instruction (loops)
    uint32_t else_pos = 0;     // offset of first else-body instruction, or 0xFFFFFFFF
};
// Advance `pc` past ONE instruction (its immediate operands). Mirror of the
// interpreter's operand decoding; needed so build_blocks only sees control
// opcodes, not operand bytes that happen to equal 0x02/0x03/0x04.
static uint32_t instr_end(const uint8_t* body, uint32_t pc, uint32_t len) {
    if (pc >= len) return pc;
    uint8_t op = body[pc];
    uint32_t after = pc + 1;
    bool ok = true;
    switch (op) {
        case 0x02: case 0x03: case 0x04: after = pc + 2; break;  // opcode + 1-byte blocktype
        case 0x0C: case 0x0D: case 0x1C: case 0x1D: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
            rdleb_i(body, after, len, ok); break;
        case 0x0E: { uint32_t n = rdleb_i(body, after, len, ok);
                     for (uint32_t i = 0; i < n; i++) rdleb_i(body, after, len, ok);
                     rdleb_i(body, after, len, ok); break; }
        case 0x10: case 0x12: rdleb_i(body, after, len, ok); break;
        case 0x11: case 0x13: rdleb_i(body, after, len, ok); rdleb_i(body, after, len, ok); break;
        case 0x41: case 0x42: rdsleb_i(body, after, len, ok); break;
        case 0x43: after += 4; break;
        case 0x44: after += 8; break;
        case 0x3F: case 0x40: rdleb_i(body, after, len, ok); break;
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
            rdleb_i(body, after, len, ok); rdleb_i(body, after, len, ok); break;  // memarg align+offset
        default: break;  // no immediate operands
    }
    return after;
}
static void build_blocks(const uint8_t* body, uint32_t len, std::unordered_map<uint32_t, BlockInfo>& blocks) {
    struct Fr { uint8_t kind; uint32_t header; uint32_t loop_start; uint32_t else_pos; };
    std::vector<Fr> st;
    uint32_t pc = 0;
    while (pc < len) {
        uint8_t op = body[pc];
        if (op == 0x02 || op == 0x03 || op == 0x04) {        // block/loop/if
            uint32_t header = pc;
            uint32_t next = instr_end(body, pc, len);
            st.push_back({ op, header, next, 0xFFFFFFFFu });
            pc = next;
        } else if (op == 0x05) {                             // else
            if (!st.empty() && st.back().kind == 0x04) st.back().else_pos = pc + 1;
            pc = instr_end(body, pc, len);
        } else if (op == 0x0B) {                             // end
            if (st.empty()) break;
            Fr fr = st.back(); st.pop_back();
            BlockInfo bi; bi.kind = fr.kind; bi.end = pc;    // end = address of the 0x0B byte
            bi.loop_start = fr.loop_start; bi.else_pos = fr.else_pos;
            blocks[fr.header] = bi;
            pc += 1;
        } else {
            pc = instr_end(body, pc, len);
        }
    }
}

static int parse(const uint8_t* bytes, uint32_t len, Module& m) {
    const uint8_t* p = bytes, *end = bytes + len;
    if (len < 8 || p[0] != 0x00 || p[1] != 0x61 || p[2] != 0x73 || p[3] != 0x6D) return 1;
    p += 4;
    if (p[0] != 1 || p[1] != 0 || p[2] != 0 || p[3] != 0) return 2;
    p += 4;
    while (p < end) {
        uint8_t id = *p++;
        bool ok; uint32_t slen = rd_leb(p, end, ok);
        if (!ok || p + slen > end) return 3;
        const uint8_t* q = p, *se = p + slen; p += slen;
        if (id == 1) { // type
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                if (q >= se || *q++ != 0x60) return 4;
                bool b2; uint32_t np = rd_leb(q, se, b2);
                FuncType ft;
                for (uint32_t j = 0; j < np; j++) ft.params.push_back(*q++);
                uint32_t nr = rd_leb(q, se, b2);
                for (uint32_t j = 0; j < nr; j++) ft.results.push_back(*q++);
                m.types.push_back(ft);
            }
        } else if (id == 3) { // function
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) { bool b2; m.func_type.push_back(rd_leb(q, se, b2)); }
        } else if (id == 2) { // import
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t ml = rd_leb(q, se, bok);
                std::string mod((const char*)q, ml); q += ml;
                uint32_t nl = rd_leb(q, se, bok);
                std::string fld((const char*)q, nl); q += nl;
                uint8_t kind = *q++;
                if (kind == 0) { bool b2; uint32_t t = rd_leb(q, se, b2); m.import_funcs.push_back({ mod, fld, t }); }
                else if (kind == 3) { uint8_t vt = *q++; bool mut = *q++ != 0; m.import_globals.push_back({ mod, fld, vt, mut }); }
                else if (kind == 2) { bool b2; rd_leb(q, se, b2); uint32_t mn = rd_leb(q, se, b2); uint32_t mx = 0; if (q < se && (q[-1] & 1)) { bool b3; mx = rd_leb(q, se, b3); } (void)mx; m.has_import_mem = true; m.import_mem = { mod, fld, 0, false }; (void)mn; }
                else if (kind == 1) { bool b2; uint8_t et = *q++; (void)et; uint32_t flags = rd_leb(q, se, b2); uint32_t mn = rd_leb(q, se, b2); uint32_t mx = (flags & 1) ? rd_leb(q, se, b2) : 0; (void)mx; m.has_import_table = true; m.import_table = { mod, fld, 0, false }; (void)mn; }
            }
        } else if (id == 6) { // global
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                Global g; g.type = *q++; g.mut = *q++ != 0; g.init = 0;
                uint8_t op = *q++;
                if (op == 0x41) { bool b2; g.init = (uint32_t)rd_sleb(q, se, b2); }
                else if (op == 0x42) { bool b2; g.init = (uint64_t)rd_sleb(q, se, b2); }
                else if (op == 0x43) { bool b2; float f = (float)rd_f32(q, se, b2); memcpy(&g.init, &f, 4); }
                else if (op == 0x44) { bool b2; double d = rd_f64(q, se, b2); memcpy(&g.init, &d, 8); }
                q++; // end
                m.globals.push_back(g);
            }
        } else if (id == 5) { // memory
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t flags = rd_leb(q, se, bok);
                uint32_t min = rd_leb(q, se, bok);
                uint32_t max = (flags & 1) ? rd_leb(q, se, bok) : 0;
                m.has_mem = true; m.mem_size = min * 65536u; m.mem_max = max * 65536u;
                if (!m.mem) { m.mem = (uint8_t*)calloc(1, m.mem_size); }
            }
        } else if (id == 7) { // export
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t nl = rd_leb(q, se, bok);
                std::string name((const char*)q, nl); q += nl;
                uint8_t kind = *q++;
                uint32_t idx = rd_leb(q, se, bok);
                m.exports.emplace_back(name, std::make_pair(kind, idx));
            }
        } else if (id == 4) { // table
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint8_t et = *q++;
                uint32_t flags = rd_leb(q, se, bok);
                uint32_t min = rd_leb(q, se, bok);
                uint32_t max = (flags & 1) ? rd_leb(q, se, bok) : 0;
                (void)et; (void)max;
                m.has_table = true; m.table.assign(min, 0xFFFFFFFFu);
            }
        } else if (id == 10) { // code
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t bs = rd_leb(q, se, bok);
                const uint8_t* be = q + bs;
                Func f; f.type = (i < m.func_type.size()) ? m.func_type[i] : 0;
                uint32_t lc = rd_leb(q, se, bok);
                for (uint32_t j = 0; j < lc && bok; j++) {
                    uint32_t cnt = rd_leb(q, se, bok);
                    uint8_t lt = *q++;
                    f.local_count += cnt;
                    for (uint32_t k = 0; k < cnt; k++) f.local_types.push_back(lt);
                }
                if (be >= q) f.body.assign(q, be);
                q = be;
                m.funcs.push_back(f);
            }
        } else if (id == 11) { // data
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t flags = rd_leb(q, se, bok);
                if (flags & 1) { rd_leb(q, se, bok); }
                uint8_t op = *q++;
                bool b2; uint32_t off = (op == 0x41) ? (uint32_t)rd_sleb(q, se, b2) : 0;
                q++; // end
                uint32_t dlen = rd_leb(q, se, bok);
                if (m.mem && m.mem_size > 0) {
                    if (off + dlen <= m.mem_size) memcpy(m.mem + off, q, dlen);
                    else if (off < m.mem_size) memcpy(m.mem + off, q, m.mem_size - off);
                }
                q += dlen;
            }
        } else if (id == 9) { // elem
            bool bok; uint32_t n = rd_leb(q, se, bok);
            for (uint32_t i = 0; i < n && bok; i++) {
                uint32_t flags = rd_leb(q, se, bok);
                uint32_t tbidx = 0; uint8_t reft = 0x70; uint32_t off = 0;
                if (flags & 1) { tbidx = rd_leb(q, se, bok); }
                if (flags & 2) { reft = *q++; }
                if (!(flags & 4)) {   // active segment: offset expression
                    if (q < se) { uint8_t op = *q++;
                        bool b2;
                        if (op == 0x41) off = (uint32_t)rd_sleb(q, se, b2);
                        else if (op == 0x42) off = (uint32_t)rd_sleb(q, se, b2);
                    }
                    if (q < se && *q == 0x0B) q++;
                } else if ((flags & 4) && (flags & 8)) {   // declarative+passive forms
                    // passive (no table write); declared functions below
                } else if (flags & 8) {  // declared (0x08)
                    // declared segment: no table update
                }
                (void)tbidx; (void)reft;
                uint32_t cnt = rd_leb(q, se, bok);
                for (uint32_t j = 0; j < cnt; j++) {
                    bool b2; uint32_t fi = rd_leb(q, se, b2);
                    if (m.has_table && off + j < m.table.size()) m.table[off + j] = fi;
                }
            }
        } else if (id == 8) { // start
            bool bok; m.start_idx = rd_leb(q, se, bok);
        }
    }
    if ((m.has_mem || m.has_import_mem) && !m.mem) {
        uint32_t cap = m.mem_size ? m.mem_size : (65536 * 4);
        m.mem = (uint8_t*)calloc(1, cap);
        m.mem_size = m.mem_size ? m.mem_size : cap;
    }
    return 0;
}

// ---- interpreter ----
struct VM {
    const Module* m;
    std::vector<uint64_t> stack;
    std::vector<uint64_t> locals;
    std::vector<uint64_t> globals;    // import_globals + defined, absolute index
    std::vector<uint32_t> base_stack;
    uint64_t call_result = 0; bool has_result = false;
    bool trap = false; uint32_t steps = 0;
    uint32_t trap_op = 0; uint32_t trap_pc = 0;   // first-trap location (debugging)
    uint32_t depth = 0;              // call recursion depth guard (avoid native stack overflow)
    static const uint32_t MAX_DEPTH = 50000;

    void push(uint64_t v){ stack.push_back(v); }
    uint64_t pop(){ if (stack.empty()){ trap=true; return 0; } uint64_t v=stack.back(); stack.pop_back(); return v; }
    uint64_t top(){ return stack.empty() ? 0 : stack.back(); }

    const FuncType& type_of(uint32_t fi) const {
        if (fi < m->import_funcs.size()) return m->types[m->import_funcs[fi].type];
        uint32_t di = fi - m->import_funcs.size();
        static FuncType empty;
        return (di < m->funcs.size()) ? m->types[m->funcs[di].type] : empty;
    }

    // invoke a function in the full index space; pushes results on success.
    void invoke(uint32_t fi, const uint64_t* args) {
        if (++depth > MAX_DEPTH) { trap = true; --depth; return; }
        if (fi < m->import_funcs.size()) {
            const FuncType& it = m->types[m->import_funcs[fi].type];
            for (uint32_t i = 0; i < it.results.size(); i++) push(0);   // import stub -> 0
            --depth; return;
        }
        uint32_t di = fi - m->import_funcs.size();
        if (di >= m->funcs.size()) { trap = true; --depth; return; }
        const Func& f = m->funcs[di];
        uint64_t r = call(f, args);
        if (trap) { --depth; return; }
        if (!m->types[f.type].results.empty()) push(r);
        --depth;
    }

    uint64_t call(const Func& f, const uint64_t* args) {
        const FuncType& ft = m->types[f.type];
        base_stack.push_back((uint32_t)stack.size());
        uint32_t saved_base = base_stack.size() - 1;
        std::vector<uint64_t> frame; frame.reserve(ft.params.size() + f.local_count);
        for (uint32_t i = 0; i < ft.params.size(); i++) frame.push_back(args ? args[i] : 0);
        for (uint32_t i = 0; i < f.local_count; i++) frame.push_back(0);
        std::vector<uint64_t> saved_locals = std::move(locals);
        locals.swap(frame);
        if (stack.size() > base_stack[saved_base]) stack.resize(base_stack[saved_base]);

        const uint8_t* body = f.body.data();
        uint32_t blen = (uint32_t)f.body.size();
        // structured control frames + precomputed branch targets
        std::unordered_map<uint32_t, BlockInfo> blocks;
        build_blocks(body, blen, blocks);
        struct CF { uint32_t entry; uint32_t arity; BlockInfo bi; };
        std::vector<CF> ctrl;
        auto blocktype_arity = [&](uint8_t bt)->uint32_t {
            if (bt == 0x40) return 0;
            if (bt == T_I32 || bt == T_I64 || bt == T_F32 || bt == T_F64) return 1;
            return 0;   // multi-value via type index not needed for MVP
        };
        // branch to a depth; returns new pc via reference
        auto do_br = [&](uint32_t depth, uint32_t& pc)->void {
            if (depth >= ctrl.size()) { trap = true; return; }
            uint32_t idx = ctrl.size() - 1 - depth;
            // Copy all fields out of the frame BEFORE resizing ctrl (resize may
            // reallocate the vector and invalidate any held reference -> UB, and
            // -O2 miscompiles the dangling access).
            uint8_t fkind = ctrl[idx].bi.kind;
            uint32_t fls  = ctrl[idx].bi.loop_start;
            uint32_t fend = ctrl[idx].bi.end;
            uint32_t fentry = ctrl[idx].entry;
            uint64_t res[8]; uint32_t rn = 0;
            for (uint32_t i = 0; i < ctrl[idx].arity && !stack.empty(); i++) res[rn++] = pop();
            ctrl.resize(idx + 1);
            if (stack.size() > fentry) stack.resize(fentry);
            for (int i = (int)rn - 1; i >= 0; i--) push(res[i]);
            pc = (fkind == 0x03) ? fls : fend;
        };

        uint64_t result = 0; bool lhas = false;
        uint32_t pc = 0;
        auto rdleb = [&](bool& ok)->uint32_t{ return rdleb_i(body, pc, blen, ok); };
        const char* msc = getenv("NFU_WASM_MAXSTEP");
        uint32_t maxstep = (msc && *msc) ? (uint32_t)strtoul(msc, nullptr, 10) : 100000000u;

        while (pc < blen && !trap && steps < maxstep) {
            steps++;
            uint32_t opAddr = pc;
            uint8_t op = body[pc++];
            bool ok = true;
            switch (op) {
                case 0x00: trap = true; break;          // unreachable
                case 0x01: break;                       // nop
                case 0x02: case 0x03: {                 // block / loop
                    uint8_t bt = body[pc++];
                    ctrl.push_back({ (uint32_t)stack.size(), blocktype_arity(bt), blocks[opAddr] });
                    break;
                }
                case 0x04: {                            // if
                    uint8_t bt = body[pc++];
                    ctrl.push_back({ (uint32_t)stack.size(), blocktype_arity(bt), blocks[opAddr] });
                    uint32_t cond = (uint32_t)pop();
                    if (!cond) {
                        BlockInfo bi = blocks[opAddr];
                        pc = (bi.else_pos != 0xFFFFFFFFu) ? bi.else_pos : bi.end;
                    }
                    break;
                }
                case 0x05:                              // else -> jump to matching end
                    if (!ctrl.empty()) pc = ctrl.back().bi.end;
                    break;
                case 0x0B:                              // end
                    if (!ctrl.empty()) ctrl.pop_back();
                    break;
                case 0x0C: { uint32_t d = rdleb(ok); do_br(d, pc); break; }            // br
                case 0x0D: { uint32_t d = rdleb(ok); uint32_t c = (uint32_t)pop(); if (c) do_br(d, pc); break; } // br_if
                case 0x0E: {                            // br_table
                    uint32_t n = rdleb(ok);
                    uint32_t labs[32]; for (uint32_t i = 0; i < n && i < 32; i++) labs[i] = rdleb(ok);
                    uint32_t def = rdleb(ok);
                    uint32_t i = (uint32_t)pop();
                    do_br((i < n) ? labs[i] : def, pc);
                    break;
                }
                case 0x0F: {                            // return
                    if (!ft.results.empty() && !stack.empty()) { result = stack.back(); lhas = true; }
                    pc = blen; break;
                }
                case 0x10: {                            // call
                    uint32_t fi = rdleb(ok);
                    const FuncType& ct = type_of(fi);
                    uint64_t a[16] = {0};
                    for (int i = (int)ct.params.size() - 1; i >= 0; i--) a[i] = pop();
                    invoke(fi, a);
                    break;
                }
                case 0x11: {                            // call_indirect
                    uint32_t tidx = rdleb(ok);
                    uint32_t tbl = rdleb(ok);
                    uint32_t elem = (uint32_t)pop();
                    if (tbl == 0 && elem < m->table.size() && m->table[elem] != 0xFFFFFFFFu) {
                        uint32_t fi = m->table[elem];
                        if (type_of(fi).params.size() != m->types[tidx].params.size()) { trap = true; break; }
                        const FuncType& ct = type_of(fi);
                        uint64_t a[16] = {0};
                        for (int i = (int)ct.params.size() - 1; i >= 0; i--) a[i] = pop();
                        invoke(fi, a);
                    } else trap = true;
                    break;
                }
                case 0x12: {                            // return_call (tail call)
                    uint32_t fi = rdleb(ok);
                    const FuncType& ct = type_of(fi);
                    uint64_t a[16] = {0};
                    for (int i = (int)ct.params.size() - 1; i >= 0; i--) a[i] = pop();
                    invoke(fi, a);
                    pc = blen; break;
                }
                case 0x13: {                            // return_call_indirect
                    uint32_t tidx = rdleb(ok), tbl = rdleb(ok);
                    uint32_t elem = (uint32_t)pop();
                    if (tbl == 0 && elem < m->table.size() && m->table[elem] != 0xFFFFFFFFu) {
                        uint32_t fi = m->table[elem];
                        const FuncType& ct = type_of(fi);
                        uint64_t a[16] = {0};
                        for (int i = (int)ct.params.size() - 1; i >= 0; i--) a[i] = pop();
                        invoke(fi, a);
                    } else trap = true;
                    pc = blen; break;
                }
                case 0x1A: pop(); break;                // drop
                case 0x1B: { uint64_t c = pop(), b = pop(), a = pop(); push(c ? a : b); break; } // select
                case 0x1C: case 0x1D: { rdleb(ok); uint64_t c = pop(), b = pop(), a = pop(); push(c ? a : b); break; } // select (typed)
                case 0x20: { uint32_t i = rdleb(ok); if (i < locals.size()) push(locals[i]); break; }
                case 0x21: { uint32_t i = rdleb(ok); if (i < locals.size()) locals[i] = pop(); break; }
                case 0x22: { uint32_t i = rdleb(ok); if (i < locals.size()) { uint64_t v = top(); locals[i] = pop(); push(v); } break; }
                case 0x23: { uint32_t i = rdleb(ok); if (i < globals.size()) push(globals[i]); break; }
                case 0x24: { uint32_t i = rdleb(ok); if (i < globals.size()) globals[i] = pop(); break; }
                case 0x41: push((uint32_t)rdsleb_i(body, pc, blen, ok)); break;  // i32.const
                case 0x42: push((uint64_t)rdsleb_i(body, pc, blen, ok)); break;  // i64.const
                case 0x43: { bool b2; float f = (float)rdf32_i(body, pc, blen, b2); uint32_t v; memcpy(&v, &f, 4); push(v); break; }
                case 0x44: { bool b2; double d = rdf64_i(body, pc, blen, b2); uint64_t v; memcpy(&v, &d, 8); push(v); break; }
                // memory loads (memarg = align, offset)
                case 0x28: case 0x29: case 0x2A: case 0x2B:
                case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
                    rdleb_i(body, pc, blen, ok); uint32_t off = rdleb_i(body, pc, blen, ok);
                    uint64_t addr = pop() + off;
                    if (!m->mem || addr + 8 > m->mem_size) { trap = true; break; }
                    const uint8_t* d = m->mem + addr;
                    switch (op) {
                        case 0x28: { int32_t v; memcpy(&v, d, 4); push((uint64_t)(uint32_t)v); break; }       // i32.load
                        case 0x29: { uint64_t v; memcpy(&v, d, 8); push(v); break; }                            // i64.load
                        case 0x2A: { uint32_t v; memcpy(&v, d, 4); push(v); break; }                            // f32.load
                        case 0x2B: { uint64_t v; memcpy(&v, d, 8); push(v); break; }                            // f64.load
                        case 0x2C: push((uint64_t)(int32_t)(int8_t)d[0]); break;                               // i32.load8_s
                        case 0x2D: push((uint64_t)d[0]); break;                                                 // i32.load8_u
                        case 0x2E: { int16_t v; memcpy(&v, d, 2); push((uint64_t)(int32_t)v); break; }          // i32.load16_s
                        case 0x2F: { uint16_t v; memcpy(&v, d, 2); push((uint64_t)v); break; }                 // i32.load16_u
                        case 0x30: push((uint64_t)(int64_t)(int8_t)d[0]); break;                               // i64.load8_s
                        case 0x31: push((uint64_t)d[0]); break;                                                 // i64.load8_u
                        case 0x32: { int16_t v; memcpy(&v, d, 2); push((uint64_t)(int64_t)v); break; }         // i64.load16_s
                        case 0x33: { uint16_t v; memcpy(&v, d, 2); push((uint64_t)v); break; }                 // i64.load16_u
                        case 0x34: { int32_t v; memcpy(&v, d, 4); push((uint64_t)(int64_t)v); break; }         // i64.load32_s
                        default:   { uint32_t v; memcpy(&v, d, 4); push((uint64_t)v); break; }                 // i64.load32_u (0x35)
                    }
                    break;
                }
                // memory stores (memarg)
                case 0x36: case 0x37: case 0x38: case 0x39:
                case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: {
                    rdleb_i(body, pc, blen, ok); uint32_t off = rdleb_i(body, pc, blen, ok);
                    uint64_t val = pop();
                    uint64_t addr = pop() + off;
                    if (!m->mem || addr + 8 > m->mem_size) { trap = true; break; }
                    uint8_t* d = m->mem + addr;
                    switch (op) {
                        case 0x36: { int32_t v = (int32_t)(uint32_t)val; memcpy(d, &v, 4); break; }   // i32.store
                        case 0x37: memcpy(d, &val, 8); break;                                          // i64.store
                        case 0x38: { uint32_t v = (uint32_t)val; memcpy(d, &v, 4); break; }            // f32.store
                        case 0x39: memcpy(d, &val, 8); break;                                          // f64.store
                        case 0x3A: d[0] = (uint8_t)val; break;                                         // i32.store8
                        case 0x3B: { uint16_t v = (uint16_t)val; memcpy(d, &v, 2); break; }            // i32.store16
                        case 0x3C: d[0] = (uint8_t)val; break;                                         // i64.store8
                        case 0x3D: { uint16_t v = (uint16_t)val; memcpy(d, &v, 2); break; }            // i64.store16
                        default:   { uint32_t v = (uint32_t)val; memcpy(d, &v, 4); break; }            // i64.store32 (0x3E)
                    }
                    break;
                }
                case 0x3F: { rdleb_i(body, pc, blen, ok); push((uint64_t)(m->mem_size / 65536u)); break; } // memory.size
                case 0x40: { // memory.grow
                    rdleb_i(body, pc, blen, ok);
                    uint32_t d = (uint32_t)pop();
                    uint32_t old = m->mem_size / 65536u;
                    uint32_t max_pages = m->mem_max ? (m->mem_max / 65536u) : 65536u;
                    if (!m->mem || old + d > max_pages) { push(0xFFFFFFFFull); break; }
                    uint8_t* nm = (uint8_t*)realloc(m->mem, (size_t)(old + d) * 65536u);
                    if (!nm) { push(0xFFFFFFFFull); break; }
                    memset(nm + m->mem_size, 0, (size_t)d * 65536u);
                    const_cast<Module*>(m)->mem = nm;
                    const_cast<Module*>(m)->mem_size = (old + d) * 65536u;
                    push((uint64_t)old); break;
                }
                // i32 comparisons
                case 0x45: push((uint32_t)pop() == 0); break;
                case 0x46: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a == b); break; }
                case 0x47: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a != b); break; }
                case 0x48: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push((int32_t)a < (int32_t)b); break; }
                case 0x49: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a < b); break; }
                case 0x4A: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push((int32_t)a > (int32_t)b); break; }
                case 0x4B: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a > b); break; }
                case 0x4C: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push((int32_t)a <= (int32_t)b); break; }
                case 0x4D: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a <= b); break; }
                case 0x4E: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push((int32_t)a >= (int32_t)b); break; }
                case 0x4F: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a >= b); break; }
                // i64 comparisons
                case 0x50: push((uint64_t)pop() == 0); break;
                case 0x51: { uint64_t b = pop(), a = pop(); push(a == b); break; }
                case 0x52: { uint64_t b = pop(), a = pop(); push(a != b); break; }
                case 0x53: { uint64_t b = pop(), a = pop(); push((int64_t)a < (int64_t)b); break; }
                case 0x54: { uint64_t b = pop(), a = pop(); push(a < b); break; }
                case 0x55: { uint64_t b = pop(), a = pop(); push((int64_t)a > (int64_t)b); break; }
                case 0x56: { uint64_t b = pop(), a = pop(); push(a > b); break; }
                case 0x57: { uint64_t b = pop(), a = pop(); push((int64_t)a <= (int64_t)b); break; }
                case 0x58: { uint64_t b = pop(), a = pop(); push(a <= b); break; }
                case 0x59: { uint64_t b = pop(), a = pop(); push((int64_t)a >= (int64_t)b); break; }
                case 0x5A: { uint64_t b = pop(), a = pop(); push(a >= b); break; }
                // f32 comparisons
                case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x60: {
                    uint32_t y = (uint32_t)pop(), x = (uint32_t)pop();
                    float b, a; memcpy(&b, &y, 4); memcpy(&a, &x, 4);
                    bool r = (op == 0x5B) ? (a == b) : (op == 0x5C) ? (a != b) : (op == 0x5D) ? (a < b) :
                             (op == 0x5E) ? (a > b) : (op == 0x5F) ? (a <= b) : (a >= b);
                    push(r); break;
                }
                // f64 comparisons
                case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: {
                    uint64_t y = pop(), x = pop(); double b, a; memcpy(&b, &y, 8); memcpy(&a, &x, 8);
                    bool r = (op == 0x61) ? (a == b) : (op == 0x62) ? (a != b) : (op == 0x63) ? (a < b) :
                             (op == 0x64) ? (a > b) : (op == 0x65) ? (a <= b) : (a >= b);
                    push(r); break;
                }
                // i32 arithmetic
                case 0x67: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a + b); break; }
                case 0x68: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a - b); break; }
                case 0x69: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); push(a * b); break; }
                case 0x6A: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop();
                             if (!b || (a == 0x80000000u && (int32_t)b == -1)) { trap = true; break; }
                             push((uint32_t)((int32_t)a / (int32_t)b)); break; }
                case 0x6B: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); if (!b) { trap = true; break; } push(a / b); break; }
                case 0x6C: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop();
                             if (!b) { trap = true; break; }
                             push((uint32_t)((int32_t)a % (int32_t)b)); break; }
                case 0x6D: { uint32_t b = (uint32_t)pop(), a = (uint32_t)pop(); if (!b) { trap = true; break; } push(a % b); break; }
                case 0x6E: case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: {
                    uint32_t b = (uint32_t)pop(), a = (uint32_t)pop();
                    if (op == 0x6E) push(a & b);
                    else if (op == 0x6F) push(a | b);
                    else if (op == 0x70) push(a ^ b);
                    else if (op == 0x71) push(a << (b & 31));
                    else if (op == 0x72) push((uint32_t)((int32_t)a >> (b & 31)));
                    else if (op == 0x73) push(a >> (b & 31));
                    else if (op == 0x74) { uint32_t r = (b & 31) ? ((a >> (b & 31)) | (a << (32 - (b & 31)))) : a; push(r); }
                    else { uint32_t r = (b & 31) ? ((a << (b & 31)) | (a >> (32 - (b & 31)))) : a; push(r); }
                    break;
                }
                case 0x76: { uint32_t a = (uint32_t)pop(); push(a ? (uint32_t)__builtin_clz(a) : 32u); break; }
                case 0x77: { uint32_t a = (uint32_t)pop(); push(a ? (uint32_t)__builtin_ctz(a) : 32u); break; }
                case 0x78: { uint32_t a = (uint32_t)pop(); uint32_t c = a; c = c - ((c >> 1) & 0x55555555); c = (c & 0x33333333) + ((c >> 2) & 0x33333333); c = (c + (c >> 4)) & 0x0F0F0F0F; push(c * 0x01010101 >> 24); break; }
                // i64 bit ops + clz/ctz/popcnt
                case 0x79: { uint64_t a = pop(); push(a ? (uint64_t)__builtin_clzll(a) : 64u); break; }
                case 0x7A: { uint64_t a = pop(); push(a ? (uint64_t)__builtin_ctzll(a) : 64u); break; }
                case 0x7B: { uint64_t a = pop(); uint64_t c = a; c = c - ((c >> 1) & 0x5555555555555555ULL); c = (c & 0x3333333333333333ULL) + ((c >> 2) & 0x3333333333333333ULL); c = (c + (c >> 4)) & 0x0F0F0F0F0F0F0F0FULL; push(c * 0x0101010101010101ULL >> 56); break; }
                // i64 arithmetic
                case 0x7C: { uint64_t b = pop(), a = pop(); push(a + b); break; }
                case 0x7D: { uint64_t b = pop(), a = pop(); push(a - b); break; }
                case 0x7E: { uint64_t b = pop(), a = pop(); push(a * b); break; }
                case 0x7F: { uint64_t b = pop(), a = pop();
                             if (!b || (a == 0x8000000000000000ULL && (int64_t)b == -1)) { trap = true; break; }
                             push((uint64_t)((int64_t)a / (int64_t)b)); break; }
                case 0x80: { uint64_t b = pop(), a = pop(); if (!b) { trap = true; break; } push(a / b); break; }
                case 0x81: { uint64_t b = pop(), a = pop(); if (!b) { trap = true; break; } push((uint64_t)((int64_t)a % (int64_t)b)); break; }
                case 0x82: { uint64_t b = pop(), a = pop(); if (!b) { trap = true; break; } push(a % b); break; }
                case 0x83: { uint64_t b = pop(), a = pop(); push(a & b); break; }
                case 0x84: { uint64_t b = pop(), a = pop(); push(a | b); break; }
                case 0x85: { uint64_t b = pop(), a = pop(); push(a ^ b); break; }
                case 0x86: { uint64_t b = pop(), a = pop(); push(a << (b & 63)); break; }
                case 0x87: { uint64_t b = pop(), a = pop(); push((uint64_t)((int64_t)a >> (b & 63))); break; }
                case 0x88: { uint64_t b = pop(), a = pop(); push(a >> (b & 63)); break; }
                case 0x89: { uint64_t b = pop(), a = pop(); uint64_t r = (b & 63) ? ((a >> (b & 63)) | (a << (64 - (b & 63)))) : a; push(r); break; }
                case 0x8A: { uint64_t b = pop(), a = pop(); uint64_t r = (b & 63) ? ((a << (b & 63)) | (a >> (64 - (b & 63)))) : a; push(r); break; }
                // f32 arithmetic
                case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: case 0x90: case 0x91: {
                    uint32_t y = (uint32_t)pop(), x = (uint32_t)pop(); float b, a, r;
                    memcpy(&b, &y, 4); memcpy(&a, &x, 4);
                    if (op == 0x8B) r = a + b;
                    else if (op == 0x8C) r = a - b;
                    else if (op == 0x8D) r = a * b;
                    else if (op == 0x8E) r = b ? a / b : 0;
                    else if (op == 0x8F) r = a < b ? a : b;
                    else if (op == 0x90) r = a > b ? a : b;
                    else { int32_t ab, bb; memcpy(&ab, &a, 4); memcpy(&bb, &b, 4); r = ((ab < 0) != (bb < 0)) ? -a : a; }
                    uint32_t v; memcpy(&v, &r, 4); push(v); break;
                }
                // f64 arithmetic
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: case 0x98: {
                    uint64_t y = pop(), x = pop(); double b, a, r;
                    memcpy(&b, &y, 8); memcpy(&a, &x, 8);
                    if (op == 0x92) r = a + b;
                    else if (op == 0x93) r = a - b;
                    else if (op == 0x94) r = a * b;
                    else if (op == 0x95) r = b ? a / b : 0;
                    else if (op == 0x96) r = a < b ? a : b;
                    else if (op == 0x97) r = a > b ? a : b;
                    else { int64_t ab, bb; memcpy(&ab, &a, 8); memcpy(&bb, &b, 8); r = ((ab < 0) != (bb < 0)) ? -a : a; }
                    uint64_t v; memcpy(&v, &r, 8); push(v); break;
                }
                // conversions (full MVP set)
                case 0xA7: push((uint32_t)pop()); break;                            // i32.wrap_i64
                case 0xA8: { uint32_t x = (uint32_t)pop(); float f; memcpy(&f, &x, 4); push((uint64_t)(int32_t)f); break; }  // i32.trunc_f32_s
                case 0xA9: { uint32_t x = (uint32_t)pop(); float f; memcpy(&f, &x, 4); push((uint64_t)(uint32_t)f); break; }  // i32.trunc_f32_u
                case 0xAA: { uint64_t x = pop(); double d; memcpy(&d, &x, 8); push((uint64_t)(int32_t)d); break; }           // i32.trunc_f64_s
                case 0xAB: { uint64_t x = pop(); double d; memcpy(&d, &x, 8); push((uint64_t)(uint32_t)d); break; }           // i32.trunc_f64_u
                case 0xAC: { int32_t a = (int32_t)(uint32_t)pop(); push((uint64_t)(int64_t)a); break; }                       // i64.extend_i32_s
                case 0xAD: { uint32_t a = (uint32_t)pop(); push((uint64_t)a); break; }                                        // i64.extend_i32_u
                case 0xAE: { uint32_t x = (uint32_t)pop(); float f; memcpy(&f, &x, 4); push((uint64_t)(int64_t)f); break; }   // i64.trunc_f32_s
                case 0xAF: { uint32_t x = (uint32_t)pop(); float f; memcpy(&f, &x, 4); push((uint64_t)(uint64_t)f); break; }  // i64.trunc_f32_u
                case 0xB0: { uint64_t x = pop(); double d; memcpy(&d, &x, 8); push((uint64_t)(int64_t)d); break; }            // i64.trunc_f64_s
                case 0xB1: { uint64_t x = pop(); double d; memcpy(&d, &x, 8); push((uint64_t)d); break; }                     // i64.trunc_f64_u
                case 0xB2: { int32_t a = (int32_t)(uint32_t)pop(); float f = (float)a; uint32_t v; memcpy(&v, &f, 4); push(v); break; } // f32.convert_i32_s
                case 0xB3: { uint32_t a = (uint32_t)pop(); float f = (float)a; uint32_t v; memcpy(&v, &f, 4); push(v); break; }        // f32.convert_i32_u
                case 0xB4: { int64_t a = (int64_t)pop(); float f = (float)a; uint32_t v; memcpy(&v, &f, 4); push(v); break; }         // f32.convert_i64_s
                case 0xB5: { uint64_t a = pop(); float f = (float)a; uint32_t v; memcpy(&v, &f, 4); push(v); break; }                 // f32.convert_i64_u
                case 0xB6: { uint64_t x = pop(); double d; memcpy(&d, &x, 8); float f = (float)d; uint32_t v; memcpy(&v, &f, 4); push(v); break; } // f32.demote_f64
                case 0xB7: { int32_t a = (int32_t)(uint32_t)pop(); double d = (double)a; uint64_t v; memcpy(&v, &d, 8); push(v); break; } // f64.convert_i32_s
                case 0xB8: { uint32_t a = (uint32_t)pop(); double d = (double)a; uint64_t v; memcpy(&v, &d, 8); push(v); break; }       // f64.convert_i32_u
                case 0xB9: { int64_t a = (int64_t)pop(); double d = (double)a; uint64_t v; memcpy(&v, &d, 8); push(v); break; }        // f64.convert_i64_s
                case 0xBA: { uint64_t a = pop(); double d = (double)a; uint64_t v; memcpy(&v, &d, 8); push(v); break; }                // f64.convert_i64_u
                case 0xBB: push(pop()); break;                                  // f64.reinterpret_i64 (bitcast no-op on u64)
                case 0xBC: { uint32_t x = (uint32_t)pop(); push(x); break; }    // i32.reinterpret_f32 (bitcast)
                case 0xBD: push(pop()); break;                                  // i64.reinterpret_f64 (bitcast)
                // sign-extension
                case 0xC0: { int32_t a = (int32_t)(uint32_t)pop(); push((uint32_t)(int32_t)(int8_t)a); break; }
                case 0xC1: { int32_t a = (int32_t)(uint32_t)pop(); push((uint32_t)(int32_t)(int16_t)a); break; }
                case 0xC2: { int64_t a = (int64_t)pop(); push((uint64_t)(int8_t)a); break; }
                case 0xC3: { int64_t a = (int64_t)pop(); push((uint64_t)(int16_t)a); break; }
                case 0xC4: { int64_t a = (int64_t)pop(); push((uint64_t)(int32_t)a); break; }
                default: trap = true; break;   // unknown opcode -> fail loudly
            }
            if (trap && !trap_op) { trap_op = op; trap_pc = opAddr; }
        }
        if (!lhas && !ft.results.empty() && !stack.empty()) { result = stack.back(); lhas = true; }
        if (stack.size() > base_stack[saved_base]) stack.resize(base_stack[saved_base]);
        locals = std::move(saved_locals);
        base_stack.pop_back();
        return result;
    }

    uint64_t call_export(const std::string& name, const uint64_t* args) {
        for (auto& e : m->exports) {
            if (e.first == name && e.second.first == 0) {
                uint32_t fi = e.second.second;
                if (fi < m->import_funcs.size()) {
                    const FuncType& it = m->types[m->import_funcs[fi].type];
                    if (!it.results.empty()) return 0;
                    return 0;
                }
                uint32_t di = fi - m->import_funcs.size();
                if (di >= m->funcs.size()) return 0;
                call_result = 0; has_result = false;
                return call(m->funcs[di], args);
            }
        }
        return 0;
    }
};

// ---- stateful instance: keeps memory + mut globals across export calls ----
struct Instance {
    Module m;
    std::vector<uint64_t> globals;
    bool start_run = false;
    void init() {
        globals.resize(m.global_count(), 0);
        for (uint32_t i = 0; i < m.globals.size(); i++) globals[m.import_globals.size() + i] = m.globals[i].init;
    }
};

static int run_instance(Instance& inst, const char* fname, const uint64_t* args, uint64_t& out) {
    VM vm; vm.m = &inst.m;
    vm.globals = inst.globals;
    // run the start function once at instantiation time
    if (!inst.start_run && inst.m.start_idx != 0xFFFFFFFFu) {
        inst.start_run = true;
        VM svm; svm.m = &inst.m; svm.globals = inst.globals;
        uint32_t fi = inst.m.start_idx;
        if (fi < inst.m.import_funcs.size()) {
            // imported start -> no-op stub
        } else {
            uint32_t di = fi - inst.m.import_funcs.size();
            if (di < inst.m.funcs.size()) svm.call(inst.m.funcs[di], nullptr);
        }
        if (!svm.trap) inst.globals = svm.globals;
    }
    out = vm.call_export(fname, args);
    inst.globals = vm.globals;          // persist mut globals back to the instance
    return vm.trap ? -1 : 0;
}

// stateless one-shot helper (still used by the standalone test harness).
// Runs directly on the caller's module; memory/globals do NOT persist.
static int run_module(Module& m, const char* fname, const uint64_t* args, uint64_t& out) {
    VM vm; vm.m = &m;
    vm.globals.assign(m.global_count(), 0);
    for (uint32_t i = 0; i < m.globals.size(); i++) vm.globals[m.import_globals.size() + i] = m.globals[i].init;
    out = vm.call_export(fname, args);
    return vm.trap ? -1 : 0;
}

} // namespace nefu_wasm
