// nefuOS mini JVM - bytecode interpreter and public entry point.
// Built-in class dispatch lives in minijvm_builtin.cpp, helpers in
// minijvm_core.cpp, soft double math in minijvm_math.h.
#include "minijvm_internal.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

const MiniJvmVfs* g_minijvm_vfs_hooks = 0;

namespace {

// ================= interpreter =================
static int run_method(VM* vm, int mi) {
    if (mi < 0 || mi >= vm->cls.method_count) { vm_set_err(vm, "bad method index"); return -1; }
    MethodInfo* m = &vm->cls.methods[mi];
    if (!m->code) { vm_set_err(vm, "method has no code"); return -1; }
    Frame* f = &vm->frames[vm->depth - 1];
    const uint8_t* code = m->code;
    const uint32_t clen = m->code_len;
    ClassInfo* c = &vm->cls;

    while (f->pc < (int)clen || vm->has_exc) {
        if (vm->done) return 0;
        if (vm->has_exc) {
            int r = propagate_exc(vm);
            if (r == -2) return -2;
            if (r != 0) return -1;
            f = &vm->frames[vm->depth - 1];
            continue;
        }
        if (++vm->steps > MAX_STEPS) { vm_set_err(vm, "step limit exceeded"); return -1; }
        uint32_t pc = (uint32_t)f->pc;
        uint8_t op = code[pc];
        f->pc++;
        switch (op) {
        // ---- constants ----
        case 0x00: break;                                   // nop
        case 0x01: vm_push(vm, V_NULL()); break;            // aconst_null
        case 0x02: vm_push(vm, V_INT(-1)); break;
        case 0x03: vm_push(vm, V_INT(0)); break;
        case 0x04: vm_push(vm, V_INT(1)); break;
        case 0x05: vm_push(vm, V_INT(2)); break;
        case 0x06: vm_push(vm, V_INT(3)); break;
        case 0x07: vm_push(vm, V_INT(4)); break;
        case 0x08: vm_push(vm, V_INT(5)); break;
        case 0x09: vm_push(vm, V_LONG(0)); break;
        case 0x0A: vm_push(vm, V_LONG(1)); break;
        case 0x0B: vm_push(vm, V_F32(0.0f)); break;         // fconst_0
        case 0x0C: vm_push(vm, V_F32(1.0f)); break;
        case 0x0D: vm_push(vm, V_F32(2.0f)); break;
        case 0x0E: vm_push(vm, V_F64(0.0)); break;          // dconst_0
        case 0x0F: vm_push(vm, V_F64(1.0)); break;          // dconst_1
        case 0x10: {                                        // bipush
            int8_t b = (int8_t)code[f->pc]; f->pc++;
            vm_push(vm, V_INT(b));
            break;
        }
        case 0x11: {                                        // sipush
            int16_t b = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            vm_push(vm, V_INT(b));
            break;
        }
        case 0x12: {                                        // ldc
            uint16_t idx = code[f->pc]; f->pc++;
            CpEntry* e = &c->cp[idx];
            if (e->tag == 3) vm_push(vm, V_INT(e->ival));
            else if (e->tag == 4) vm_push(vm, V_F32(mj_f_from_bits((uint32_t)e->ival)));
            else if (e->tag == 8) {
                const char* s = cp_utf8(c, e->a);
                int64_t r = mk_string(vm, s, (int)strlen(s));
                vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
            } else { vm_set_err(vm, "bad ldc"); }
            break;
        }
        case 0x13: {                                        // ldc_w
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            CpEntry* e = &c->cp[idx];
            if (e->tag == 3) vm_push(vm, V_INT(e->ival));
            else if (e->tag == 4) vm_push(vm, V_F32(mj_f_from_bits((uint32_t)e->ival)));
            else if (e->tag == 8) {
                const char* s = cp_utf8(c, e->a);
                int64_t r = mk_string(vm, s, (int)strlen(s));
                vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
            } else { vm_set_err(vm, "bad ldc_w"); }
            break;
        }
        case 0x14: {                                        // ldc2_w
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            CpEntry* e = &c->cp[idx];
            if (e->tag == 5) vm_push(vm, V_LONG(e->lval));
            else if (e->tag == 6) vm_push(vm, V_F64(mj_d_from_bits((uint64_t)e->lval)));
            else { vm_set_err(vm, "bad ldc2_w"); }
            break;
        }
        // ---- locals ----
        case 0x15: case 0x16: case 0x19: {                  // iload/lload/aload
            uint8_t i = code[f->pc]; f->pc++;
            vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i]);
            break;
        }
        case 0x17: case 0x18: {                             // fload/dload
            uint8_t i = code[f->pc]; f->pc++;
            vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i]);
            break;
        }
        case 0x1A: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0]); break;
        case 0x1B: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1]); break;
        case 0x1C: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2]); break;
        case 0x1D: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3]); break;
        case 0x1E: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0]); break;
        case 0x1F: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1]); break;
        case 0x20: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2]); break;
        case 0x21: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3]); break;
        case 0x22: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0]); break;
        case 0x23: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1]); break;
        case 0x24: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2]); break;
        case 0x25: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3]); break;
        case 0x26: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0]); break;
        case 0x27: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1]); break;
        case 0x28: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2]); break;
        case 0x29: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3]); break;
        case 0x2A: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0]); break;
        case 0x2B: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1]); break;
        case 0x2C: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2]); break;
        case 0x2D: vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3]); break;
        case 0x36: case 0x37: case 0x3A: {                  // istore/lstore/astore
            uint8_t i = code[f->pc]; f->pc++;
            vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i] = vm_pop(vm);
            break;
        }
        case 0x38: case 0x39: {                             // fstore/dstore
            uint8_t i = code[f->pc]; f->pc++;
            vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i] = vm_pop(vm);
            break;
        }
        case 0x3B: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0] = vm_pop(vm); break;
        case 0x3C: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1] = vm_pop(vm); break;
        case 0x3D: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2] = vm_pop(vm); break;
        case 0x3E: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3] = vm_pop(vm); break;
        case 0x3F: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0] = vm_pop(vm); break;
        case 0x40: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1] = vm_pop(vm); break;
        case 0x41: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2] = vm_pop(vm); break;
        case 0x42: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3] = vm_pop(vm); break;
        case 0x43: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0] = vm_pop(vm); break;
        case 0x44: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1] = vm_pop(vm); break;
        case 0x45: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2] = vm_pop(vm); break;
        case 0x46: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3] = vm_pop(vm); break;
        case 0x47: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0] = vm_pop(vm); break;
        case 0x48: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1] = vm_pop(vm); break;
        case 0x49: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2] = vm_pop(vm); break;
        case 0x4A: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3] = vm_pop(vm); break;
        case 0x4B: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 0] = vm_pop(vm); break;
        case 0x4C: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 1] = vm_pop(vm); break;
        case 0x4D: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 2] = vm_pop(vm); break;
        case 0x4E: vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + 3] = vm_pop(vm); break;
        // ---- arrays ----
        case 0x2E: {                                        // iaload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            vm_push(vm, V_INT(o->arr[i]));
            break;
        }
        case 0x2F: {                                        // laload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            vm_push(vm, V_LONG(o->larr[i]));
            break;
        }
        case 0x30: {                                        // faload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            vm_push(vm, V_F32(mj_f_from_bits((uint32_t)o->arr[i])));
            break;
        }
        case 0x31: {                                        // daload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            vm_push(vm, V_F64(mj_d_from_bits((uint64_t)o->larr[i])));
            break;
        }
        case 0x32: {                                        // aaload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            int32_t r = o->arr[i];
            vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
            break;
        }
        case 0x33: case 0x34: case 0x35: {                  // baload/caload/saload
            Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            int32_t v = o->arr[i];
            if (op == 0x33) v = (int8_t)v;
            else if (op == 0x35) v = (int16_t)v;
            vm_push(vm, V_INT(v));
            break;
        }
        case 0x4F: {                                        // iastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->arr[i] = i32(val.v);
            break;
        }
        case 0x50: {                                        // lastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->larr[i] = val.v;
            break;
        }
        case 0x51: {                                        // fastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->arr[i] = (int32_t)(uint32_t)mj_f_to_bits(mj_f_from_bits((uint64_t)(uint32_t)val.v));
            break;
        }
        case 0x52: {                                        // dastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->larr[i] = val.v;
            break;
        }
        case 0x53: {                                        // aastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->arr[i] = (val.kind == K_REF) ? i32(val.v) : -1;
            break;
        }
        case 0x54: case 0x55: case 0x56: {                  // bastore/castore/sastore
            Val val = vm_pop(vm); Val idx = vm_pop(vm); Val ref = vm_pop(vm);
            int i = i32(idx.v);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            if (i < 0 || i >= o->len) { vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "array index"); break; }
            o->arr[i] = i32(val.v);
            break;
        }
        case 0xBE: {                                        // arraylength
            Val ref = vm_pop(vm);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null array"); break; }
            vm_push(vm, V_INT(o->len));
            break;
        }
        case 0xBC: {                                        // newarray
            Val cnt = vm_pop(vm);
            int n = i32(cnt.v);
            uint8_t at = code[f->pc]; f->pc++;
            if (n < 0 || n > MAX_ARR) { vm_throw(vm, "java/lang/NegativeArraySizeException", "array size"); break; }
            if (at == 6) {
                Obj* o = vm_new_obj(vm, OBJ_INTARR);
                if (!o) break;
                o->len = n; o->cap = n;
                o->arr = (int32_t*)kalloc((size_t)n * sizeof(int32_t) + 4);
                if (o->arr) memset(o->arr, 0, (size_t)n * sizeof(int32_t));
                vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            } else if (at == 7 || at == 11) {
                Obj* o = vm_new_obj(vm, OBJ_LONGARR);
                if (!o) break;
                o->len = n; o->cap = n;
                o->larr = (int64_t*)kalloc((size_t)n * sizeof(int64_t) + 8);
                if (o->larr) memset(o->larr, 0, (size_t)n * sizeof(int64_t));
                vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            } else {
                Obj* o = vm_new_obj(vm, OBJ_INTARR);
                if (!o) break;
                o->len = n; o->cap = n;
                o->arr = (int32_t*)kalloc((size_t)n * sizeof(int32_t) + 4);
                if (o->arr) memset(o->arr, 0, (size_t)n * sizeof(int32_t));
                vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            }
            break;
        }
        case 0xBD: {                                        // anewarray
            Val cnt = vm_pop(vm);
            int n = i32(cnt.v);
            f->pc += 2;
            if (n < 0 || n > MAX_ARR) { vm_throw(vm, "java/lang/NegativeArraySizeException", "array size"); break; }
            Obj* o = vm_new_obj(vm, OBJ_STRARR);
            if (!o) break;
            o->len = n; o->cap = n;
            o->arr = (int32_t*)kalloc((size_t)n * sizeof(int32_t) + 4);
            if (o->arr) { for (int i = 0; i < n; i++) o->arr[i] = -1; }
            vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            break;
        }
        // ---- stack ops ----
        case 0x57: vm_pop(vm); break;                       // pop
        case 0x58: vm_pop(vm); vm_pop(vm); break;           // pop2
        case 0x59: { Val a = vm_pop(vm); vm_push(vm, a); vm_push(vm, a); break; } // dup
        case 0x5A: { Val v1 = vm_pop(vm); Val v2 = vm_pop(vm); vm_push(vm, v1); vm_push(vm, v2); vm_push(vm, v1); break; } // dup_x1
        case 0x5B: { Val v1 = vm_pop(vm); Val v2 = vm_pop(vm); Val v3 = vm_pop(vm); vm_push(vm, v1); vm_push(vm, v3); vm_push(vm, v2); vm_push(vm, v1); break; } // dup_x2
        case 0x5C: { Val a = vm_pop(vm); Val b = vm_pop(vm); vm_push(vm, b); vm_push(vm, a); vm_push(vm, b); vm_push(vm, a); break; } // dup2
        case 0x5D: { Val v1 = vm_pop(vm); Val v2 = vm_pop(vm); Val v3 = vm_pop(vm); vm_push(vm, v2); vm_push(vm, v1); vm_push(vm, v3); vm_push(vm, v2); vm_push(vm, v1); break; } // dup2_x1
        case 0x5E: { Val v1 = vm_pop(vm); Val v2 = vm_pop(vm); Val v3 = vm_pop(vm); Val v4 = vm_pop(vm); vm_push(vm, v2); vm_push(vm, v1); vm_push(vm, v4); vm_push(vm, v3); vm_push(vm, v2); vm_push(vm, v1); break; } // dup2_x2
        case 0x5F: { Val a = vm_pop(vm); Val b = vm_pop(vm); vm_push(vm, a); vm_push(vm, b); break; } // swap
        // ---- arithmetic ----
        case 0x60: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b + a))); break; }
        case 0x61: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b + a)); break; }
        case 0x62: { float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32(b + a)); break; }
        case 0x63: { double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(b + a)); break; }
        case 0x64: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b - a))); break; }
        case 0x65: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b - a)); break; }
        case 0x66: { float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32(b - a)); break; }
        case 0x67: { double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(b - a)); break; }
        case 0x68: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b * a))); break; }
        case 0x69: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b * a)); break; }
        case 0x6A: { float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32(b * a)); break; }
        case 0x6B: { double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(b * a)); break; }
        case 0x6C: {
            int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v);
            if (a == 0) { vm_throw(vm, "java/lang/ArithmeticException", "/ by zero"); break; }
            vm_push(vm, V_INT((int64_t)i32(b / a)));
            break;
        }
        case 0x6D: {
            int64_t a = vm_pop(vm).v, b = vm_pop(vm).v;
            if (a == 0) { vm_throw(vm, "java/lang/ArithmeticException", "/ by zero"); break; }
            vm_push(vm, V_LONG(b / a));
            break;
        }
        case 0x6E: { float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32(b / a)); break; }
        case 0x6F: { double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(b / a)); break; }
        case 0x70: {
            int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v);
            if (a == 0) { vm_throw(vm, "java/lang/ArithmeticException", "/ by zero"); break; }
            vm_push(vm, V_INT((int64_t)i32(b % a)));
            break;
        }
        case 0x71: {
            int64_t a = vm_pop(vm).v, b = vm_pop(vm).v;
            if (a == 0) { vm_throw(vm, "java/lang/ArithmeticException", "/ by zero"); break; }
            vm_push(vm, V_LONG(b % a));
            break;
        }
        case 0x72: { float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32((float)mj_rem((double)b, (double)a))); break; }
        case 0x73: { double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(mj_rem(b, a))); break; }
        case 0x74: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(-a))); break; }
        case 0x75: { int64_t a = vm_pop(vm).v; vm_push(vm, V_LONG(-a)); break; }
        case 0x76: { float a = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F32(-a)); break; }
        case 0x77: { double a = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F64(-a)); break; }
        case 0x78: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b << (a & 31)))); break; }
        case 0x79: { int32_t a = i32(vm_pop(vm).v); int64_t b = vm_pop(vm).v; vm_push(vm, V_LONG(b << (a & 63))); break; }
        case 0x7A: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b >> (a & 31)))); break; }
        case 0x7B: { int32_t a = i32(vm_pop(vm).v); int64_t b = vm_pop(vm).v; vm_push(vm, V_LONG(b >> (a & 63))); break; }
        case 0x7C: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32((uint32_t)b >> (a & 31)))); break; }
        case 0x7D: { int32_t a = i32(vm_pop(vm).v); int64_t b = vm_pop(vm).v; vm_push(vm, V_LONG((uint64_t)b >> (a & 63))); break; }
        case 0x7E: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b & a))); break; }
        case 0x7F: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b & a)); break; }
        case 0x80: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b | a))); break; }
        case 0x81: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b | a)); break; }
        case 0x82: { int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(b ^ a))); break; }
        case 0x83: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_LONG(b ^ a)); break; }
        case 0x84: {                                        // iinc
            uint8_t i = code[f->pc]; int8_t d = (int8_t)code[f->pc + 1]; f->pc += 2;
            Val* L = &vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i];
            L->v = (int64_t)i32(L->v + d);
            break;
        }
        // ---- conversions ----
        case 0x85: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_LONG((int64_t)a)); break; }
        case 0x86: { int64_t a = vm_pop(vm).v; vm_push(vm, V_INT((int64_t)i32(a))); break; }
        case 0x87: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_F32((float)a)); break; }
        case 0x88: { int64_t a = vm_pop(vm).v; vm_push(vm, V_F32((float)a)); break; }
        case 0x89: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_F64((double)a)); break; }
        case 0x8A: { int64_t a = vm_pop(vm).v; vm_push(vm, V_F64((double)a)); break; }
        case 0x8B: { float a = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(f2i_sat((double)a)))); break; }
        case 0x8C: { float a = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_LONG(f2l_sat((double)a))); break; }
        case 0x8D: { float a = mj_f_from_bits(vm_pop(vm).v); vm_push(vm, V_F64((double)a)); break; }
        case 0x8E: { double a = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32(f2i_sat(a)))); break; }
        case 0x8F: { double a = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_LONG(f2l_sat(a))); break; }
        case 0x90: { double a = mj_d_from_bits(vm_pop(vm).v); vm_push(vm, V_F32((float)a)); break; }
        case 0x91: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32((int8_t)a))); break; }
        case 0x92: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32((uint16_t)a))); break; }
        case 0x93: { int32_t a = i32(vm_pop(vm).v); vm_push(vm, V_INT((int64_t)i32((int16_t)a))); break; }
        case 0x94: { int64_t a = vm_pop(vm).v, b = vm_pop(vm).v; vm_push(vm, V_INT(b > a ? 1 : (b < a ? -1 : 0))); break; }
        case 0x95: case 0x96: {
            float a = mj_f_from_bits(vm_pop(vm).v), b = mj_f_from_bits(vm_pop(vm).v);
            if (mj_is_nan((double)a) || mj_is_nan((double)b)) { vm_push(vm, V_INT(op == 0x95 ? 1 : -1)); break; }
            vm_push(vm, V_INT(b > a ? 1 : (b < a ? -1 : 0)));
            break;
        }
        case 0x97: case 0x98: {
            double a = mj_d_from_bits(vm_pop(vm).v), b = mj_d_from_bits(vm_pop(vm).v);
            if (mj_is_nan(a) || mj_is_nan(b)) { vm_push(vm, V_INT(op == 0x97 ? 1 : -1)); break; }
            vm_push(vm, V_INT(b > a ? 1 : (b < a ? -1 : 0)));
            break;
        }
        // ---- control flow ----
        case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: {
            int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            int32_t v = i32(vm_pop(vm).v);
            bool t = (op == 0x99) ? (v == 0) : (op == 0x9A) ? (v != 0) : (op == 0x9B) ? (v < 0) :
                     (op == 0x9C) ? (v >= 0) : (op == 0x9D) ? (v > 0) : (v <= 0);
            if (t) f->pc = (int)(pc + off);
            break;
        }
        case 0x9F: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: {
            int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            int32_t a = i32(vm_pop(vm).v), b = i32(vm_pop(vm).v);
            bool t = (op == 0x9F) ? (b == a) : (op == 0xA0) ? (b != a) : (op == 0xA1) ? (b < a) :
                     (op == 0xA2) ? (b >= a) : (op == 0xA3) ? (b > a) : (b <= a);
            if (t) f->pc = (int)(pc + off);
            break;
        }
        case 0xA5: case 0xA6: {
            int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            Val a = vm_pop(vm); Val b = vm_pop(vm);
            bool eq = (a.kind == K_NULL && b.kind == K_NULL) ||
                      (a.kind == K_REF && b.kind == K_REF && a.v == b.v);
            if ((op == 0xA5 && eq) || (op == 0xA6 && !eq)) f->pc = (int)(pc + off);
            break;
        }
        case 0xC6: case 0xC7: {
            int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            Val v = vm_pop(vm);
            bool is_null = (v.kind == K_NULL);
            if ((op == 0xC6 && is_null) || (op == 0xC7 && !is_null)) f->pc = (int)(pc + off);
            break;
        }
        case 0xA7: { int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2; f->pc = (int)(pc + off); break; }
        case 0xA8: { int16_t off = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2; vm_push(vm, V_INT(f->pc)); f->pc = (int)(pc + off); break; }
        case 0xA9: { uint8_t i = code[f->pc]; f->pc++; Val* L = &vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i]; f->pc = i32(L->v); break; }
        case 0xAA: {                                        // tableswitch
            uint32_t p = pc;
            uint32_t pad = (4 - (p % 4)) % 4;
            const uint8_t* q = code + p + pad;
            if (p + pad + 12 > clen) { vm_set_err(vm, "tableswitch out of bounds"); return -1; }
            int32_t def = rd_s4(q); int32_t lo = rd_s4(q + 4); int32_t hi = rd_s4(q + 8);
            int32_t key = i32(vm_pop(vm).v);
            uint32_t cnt = (uint32_t)(hi - lo + 1);
            if (cnt > 4096 || p + pad + 12 + cnt * 4 > clen) { vm_set_err(vm, "tableswitch bad range"); return -1; }
            int32_t off = def;
            if (key >= lo && key <= hi) off = rd_s4(q + 12 + (uint32_t)(key - lo) * 4);
            f->pc = (int)(pc + off);
            break;
        }
        case 0xAB: {                                        // lookupswitch
            uint32_t p = pc;
            uint32_t pad = (4 - (p % 4)) % 4;
            const uint8_t* q = code + p + pad;
            if (p + pad + 8 > clen) { vm_set_err(vm, "lookupswitch out of bounds"); return -1; }
            int32_t def = rd_s4(q);
            int32_t npairs = rd_s4(q + 4);
            if (npairs < 0 || npairs > 4096 || p + pad + 8 + (uint32_t)npairs * 8 > clen) {
                vm_set_err(vm, "lookupswitch bad pairs"); return -1;
            }
            int32_t key = i32(vm_pop(vm).v);
            int32_t off = def;
            for (int32_t i = 0; i < npairs; i++) {
                if (rd_s4(q + 8 + (uint32_t)i * 8) == key) {
                    off = rd_s4(q + 12 + (uint32_t)i * 8);
                    break;
                }
            }
            f->pc = (int)(pc + off);
            break;
        }
        // ---- returns ----
        case 0xAC: { Val r = vm_pop(vm); vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; vm_push(vm, V_INT(sext32(r.v))); return 0; }
        case 0xAD: { Val r = vm_pop(vm); vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; vm_push(vm, V_LONG(r.v)); return 0; }
        case 0xAE: { Val r = vm_pop(vm); vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; vm_push(vm, r); return 0; }
        case 0xAF: { Val r = vm_pop(vm); vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; vm_push(vm, r); return 0; }
        case 0xB0: { Val r = vm_pop(vm); vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; vm_push(vm, r); return 0; }
        case 0xB1: { vm->depth--; if (vm->depth == 0) return 0; vm->sp = vm->frames[vm->depth].stack_base; return 0; }
        // ---- fields ----
        case 0xB2: {                                        // getstatic
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            const char* owner = cp_class_name(c, c->cp[idx].a);
            const char* fname = cp_name(c, c->cp[idx].b);
            if (strcmp(owner, "java/lang/System") == 0 && (strcmp(fname, "out") == 0 || strcmp(fname, "err") == 0)) {
                Obj* o = vm_new_obj(vm, OBJ_PRINTSTREAM);
                if (o) vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            } else if (strcmp(owner, "java/lang/Integer") == 0) {
                vm_push(vm, V_INT(strcmp(fname, "MAX_VALUE") == 0 ? 2147483647 : (int64_t)(int32_t)0x80000000u));
            } else if (strcmp(owner, "java/lang/Long") == 0) {
                vm_push(vm, V_LONG(strcmp(fname, "MAX_VALUE") == 0 ? 9223372036854775807LL : (int64_t)0x8000000000000000ULL));
            } else if (strcmp(owner, "java/lang/Boolean") == 0) {
                Val b = box_value(vm, 'Z', strcmp(fname, "TRUE") == 0 ? 1 : 0);
                vm_push(vm, b);
            } else if (strcmp(owner, "java/lang/Character") == 0) {
                vm_push(vm, V_INT(strcmp(fname, "MAX_VALUE") == 0 ? 65535 : 0));
            } else if (strcmp(owner, "java/lang/Byte") == 0) {
                vm_push(vm, V_INT(strcmp(fname, "MAX_VALUE") == 0 ? 127 : -128));
            } else if (strcmp(owner, "java/lang/Short") == 0) {
                vm_push(vm, V_INT(strcmp(fname, "MAX_VALUE") == 0 ? 32767 : -32768));
            } else if (strcmp(owner, "java/lang/Float") == 0) {
                if (strcmp(fname, "MAX_VALUE") == 0) vm_push(vm, V_F32(mj_f_from_bits(0x7F7FFFFFu)));
                else if (strcmp(fname, "MIN_VALUE") == 0) vm_push(vm, V_F32(mj_f_from_bits(0x1u)));
                else if (strcmp(fname, "POSITIVE_INFINITY") == 0) vm_push(vm, V_F32(mj_f_from_bits(0x7F800000u)));
                else if (strcmp(fname, "NEGATIVE_INFINITY") == 0) vm_push(vm, V_F32(mj_f_from_bits(0xFF800000u)));
                else vm_push(vm, V_F32(mj_f_from_bits(0x7FC00000u)));
            } else if (strcmp(owner, "java/lang/Double") == 0) {
                if (strcmp(fname, "MAX_VALUE") == 0) vm_push(vm, V_F64(mj_d_from_bits(0x7FEFFFFFFFFFFFFFULL)));
                else if (strcmp(fname, "MIN_VALUE") == 0) vm_push(vm, V_F64(mj_d_from_bits(0x1ULL)));
                else if (strcmp(fname, "POSITIVE_INFINITY") == 0) vm_push(vm, V_F64(mj_d_from_bits(0x7FF0000000000000ULL)));
                else if (strcmp(fname, "NEGATIVE_INFINITY") == 0) vm_push(vm, V_F64(mj_d_from_bits(0xFFF0000000000000ULL)));
                else vm_push(vm, V_F64(mj_d_from_bits(0x7FF8000000000000ULL)));
            } else if (strcmp(owner, "java/lang/Math") == 0) {
                vm_push(vm, V_F64(strcmp(fname, "PI") == 0 ? MJ_PI : MJ_E));
            } else {
                int slot = -1;
                for (int i = 0; i < c->field_count; i++) {
                    if (c->fields[i].is_static && strcmp(c->fields[i].name, fname) == 0) { slot = c->fields[i].slot; break; }
                }
                if (slot < 0) { vm_set_err(vm, "getstatic: field not found"); }
                else vm_push(vm, c->statics[slot]);
            }
            break;
        }
        case 0xB3: {                                        // putstatic
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            const char* fname = cp_name(c, c->cp[idx].b);
            Val v = vm_pop(vm);
            int slot = -1;
            for (int i = 0; i < c->field_count; i++) {
                if (c->fields[i].is_static && strcmp(c->fields[i].name, fname) == 0) { slot = c->fields[i].slot; break; }
            }
            if (slot < 0) { vm_set_err(vm, "putstatic: field not found"); }
            else c->statics[slot] = v;
            break;
        }
        case 0xB4: {                                        // getfield
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            const char* fname = cp_name(c, c->cp[idx].b);
            Val ref = vm_pop(vm);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null object"); break; }
            int slot = -1;
            const char* fd = 0;
            for (int i = 0; i < c->field_count; i++) {
                if (!c->fields[i].is_static && strcmp(c->fields[i].name, fname) == 0) { slot = c->fields[i].slot; fd = c->fields[i].desc; break; }
            }
            if (slot < 0 && is_throwable_like(o->cls) && o->fld_count >= 2) {
                if (strcmp(fname, "message") == 0) { slot = 0; fd = "Ljava/lang/String;"; }
                else if (strcmp(fname, "cause") == 0) { slot = 1; fd = "Ljava/lang/Throwable;"; }
            }
            if (slot < 0 || !o->flds || slot >= o->fld_count) { vm_throw(vm, "java/lang/NoSuchFieldError", "field"); break; }
            int64_t fv = o->flds[slot];
            if (fd && desc_is_ref(fd)) vm_push(vm, fv >= 0 ? V_REF(fv) : V_NULL());
            else if (fd && fd[0] == 'J') vm_push(vm, V_LONG(fv));
            else if (fd && fd[0] == 'D') vm_push(vm, V_F64(mj_d_from_bits((uint64_t)fv)));
            else if (fd && fd[0] == 'F') vm_push(vm, V_F32(mj_f_from_bits((uint64_t)(uint32_t)fv)));
            else vm_push(vm, V_INT(fv));
            break;
        }
        case 0xB5: {                                        // putfield
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            const char* fname = cp_name(c, c->cp[idx].b);
            Val v = vm_pop(vm);
            Val ref = vm_pop(vm);
            Obj* o = (ref.kind == K_REF) ? vm_obj_at(vm, ref.v) : 0;
            if (!o) { vm_throw(vm, "java/lang/NullPointerException", "null object"); break; }
            int slot = -1;
            for (int i = 0; i < c->field_count; i++) {
                if (!c->fields[i].is_static && strcmp(c->fields[i].name, fname) == 0) { slot = c->fields[i].slot; break; }
            }
            if (slot < 0 && is_throwable_like(o->cls) && o->fld_count >= 2) {
                if (strcmp(fname, "message") == 0) slot = 0;
                else if (strcmp(fname, "cause") == 0) slot = 1;
            }
            if (slot < 0 || !o->flds || slot >= o->fld_count) { vm_throw(vm, "java/lang/NoSuchFieldError", "field"); break; }
            o->flds[slot] = v.v;
            break;
        }
        // ---- methods ----
        case 0xB6: case 0xB7: case 0xB8: case 0xB9: {
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            if (op == 0xB9) f->pc += 2;
            if (idx == 0 || idx >= c->cp_count) { vm_set_err(vm, "bad method ref"); break; }
            const char* owner = cp_class_name(c, c->cp[idx].a);
            const char* mname = cp_name(c, c->cp[idx].b);
            const char* mdesc = cp_desc(c, c->cp[idx].b);
            if (!owner[0] || !mname[0]) { vm_set_err(vm, "bad method ref cp"); break; }
            bool has_this = (op != 0xB8);
            int nargs = desc_arg_slots(mdesc);
            vm->guard_n = 0;
            if (has_this) {
                Val th = vm_peek(vm, nargs + 1);
                if (th.kind == K_REF) vm->guards[vm->guard_n++] = th.v;
            }
            for (int k = 0; k < nargs; k++) {
                Val a = vm_peek(vm, nargs - k);
                if (a.kind == K_REF) vm->guards[vm->guard_n++] = a.v;
            }
            if (strcmp(owner, c->this_name) == 0) {
                int mi = find_method(c, mname, mdesc);
                if (mi < 0) { vm_set_err(vm, "user method not found"); break; }
                vm->guard_n = 0;
                if (call_method(vm, mi, nargs, has_this) == 0) {
                    int rc = run_method(vm, mi);
                    if (rc == -2) {
                        int r2 = propagate_exc(vm);
                        if (r2 == -2) return -2;
                        if (r2 != 0) return -1;
                        f = &vm->frames[vm->depth - 1];
                        continue;
                    }
                    if (rc != 0) return rc;
                    f = &vm->frames[vm->depth - 1];
                    continue;
                }
                break;
            }
            int brc = invoke_builtin(vm, owner, mname, mdesc, has_this);
            vm->guard_n = 0;
            if (brc) break;
            vm_set_err(vm, "method of unknown class");
            break;
        }
        case 0xBA: { f->pc += 4; vm_set_err(vm, "invokedynamic unsupported (lambda/method-ref)"); break; }
        // ---- objects ----
        case 0xBB: {                                        // new
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            const char* cn = cp_class_name(c, idx);
            if (strcmp(cn, "java/lang/String") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_STRING);
                if (o) { o->len = 0; o->str = (char*)kalloc(1); if (o->str) o->str[0] = 0; vm_push(vm, V_REF((int64_t)(o - vm->objs))); }
            } else if (strcmp(cn, "java/lang/StringBuilder") == 0 || strcmp(cn, "java/lang/StringBuffer") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_STRBUF);
                if (o) { o->len = 0; o->cap = 16; o->str = (char*)kalloc(17); if (o->str) o->str[0] = 0; vm_push(vm, V_REF((int64_t)(o - vm->objs))); }
            } else if (strcmp(cn, "java/util/ArrayList") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_ARR);
                if (o) { o->len = 0; o->cap = 4; o->arr = (int32_t*)kalloc(4 * sizeof(int32_t) + 4); if (o->arr) memset(o->arr, 0, 4 * sizeof(int32_t)); vm_push(vm, V_REF((int64_t)(o - vm->objs))); }
            } else if (strcmp(cn, "java/util/HashMap") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_MAP);
                if (o) { o->len = 0; o->cap = 16; o->arr = (int32_t*)kalloc(16 * 2 * sizeof(int32_t) + 4); if (o->arr) memset(o->arr, 0, 16 * 2 * sizeof(int32_t)); vm_push(vm, V_REF((int64_t)(o - vm->objs))); }
            } else if (strcmp(cn, "java/util/HashSet") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_SET);
                if (o) { o->len = 0; o->cap = 16; o->arr = (int32_t*)kalloc(16 * sizeof(int32_t) + 4); if (o->arr) memset(o->arr, 0, 16 * sizeof(int32_t)); vm_push(vm, V_REF((int64_t)(o - vm->objs))); }
            } else if (strcmp(cn, "java/io/File") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_FILE);
                if (o) vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            } else if (strcmp(cn, "java/io/FileInputStream") == 0 || strcmp(cn, "java/io/FileOutputStream") == 0 ||
                       strcmp(cn, "java/io/ByteArrayInputStream") == 0 || strcmp(cn, "java/io/ByteArrayOutputStream") == 0 ||
                       strcmp(cn, "java/io/PrintStream") == 0) {
                Obj* o = vm_new_obj(vm, OBJ_STREAM);
                if (o) {
                    o->cls = (char*)kalloc(5);
                    if (o->cls) {
                        if (strcmp(cn, "java/io/FileInputStream") == 0) strcpy(o->cls, "FIS");
                        else if (strcmp(cn, "java/io/FileOutputStream") == 0) strcpy(o->cls, "FOS");
                        else if (strcmp(cn, "java/io/ByteArrayInputStream") == 0) strcpy(o->cls, "BAIS");
                        else if (strcmp(cn, "java/io/ByteArrayOutputStream") == 0) strcpy(o->cls, "BAOS");
                        else strcpy(o->cls, "PS");
                    }
                    o->fld_count = 2;
                    o->flds = (int64_t*)kalloc(2 * sizeof(int64_t));
                    if (o->flds) { o->flds[0] = -1; o->flds[1] = 0; }
                    vm_push(vm, V_REF((int64_t)(o - vm->objs)));
                }
            } else if (is_throwable_like(cn)) {
                Obj* o = vm_new_obj(vm, OBJ_INST);
                if (o) {
                    o->cls = (char*)kalloc((size_t)strlen(cn) + 1);
                    if (o->cls) strcpy(o->cls, cn);
                    o->fld_count = 2;
                    o->flds = (int64_t*)kalloc(2 * sizeof(int64_t));
                    if (o->flds) { o->flds[0] = -1; o->flds[1] = -1; }
                    vm_push(vm, V_REF((int64_t)(o - vm->objs)));
                }
            } else {
                Obj* o = vm_new_obj(vm, OBJ_INST);
                if (o) {
                    o->cls = (char*)kalloc((size_t)strlen(cn) + 1);
                    if (o->cls) strcpy(o->cls, cn);
                    o->fld_count = (uint16_t)c->inst_field_count;
                    if (o->fld_count > 0) {
                        o->flds = (int64_t*)kalloc((size_t)o->fld_count * sizeof(int64_t));
                        if (o->flds) { for (int i = 0; i < o->fld_count; i++) o->flds[i] = -1; }
                    }
                    vm_push(vm, V_REF((int64_t)(o - vm->objs)));
                }
            }
            break;
        }
        case 0xC0: { f->pc += 2; break; }                    // checkcast (javac-verified)
        case 0xC1: {                                        // instanceof
            uint16_t idx = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            Val v = vm_pop(vm);
            int r = 0;
            const char* cn = cp_class_name(c, idx);
            if (v.kind == K_REF) {
                Obj* o = vm_obj_at(vm, v.v);
                r = type_compatible(vm, o, cn) ? 1 : 0;
            }
            vm_push(vm, V_INT(r));
            break;
        }
        case 0xC2: case 0xC3: break;                       // monitorenter / monitorexit
        case 0xC4: {                                       // wide
            uint8_t sub = code[f->pc]; f->pc++;
            uint16_t i = (uint16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
            if (sub == 0x84) {
                int16_t d = (int16_t)((code[f->pc] << 8) | code[f->pc + 1]); f->pc += 2;
                Val* L = &vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i];
                L->v = (int64_t)i32(L->v + d);
            } else {
                vm_push(vm, vm->locals[(size_t)(vm->depth - 1) * MAX_LOCALS + i]);
            }
            break;
        }
        case 0xC5: { vm_set_err(vm, "multianewarray unsupported"); break; }
        case 0xBF: {                                        // athrow
            Val e = vm_pop(vm);
            if (e.kind != K_REF) { vm_set_err(vm, "athrow of non-reference"); break; }
            vm->exc_ref = e.v;
            vm->has_exc = true;
            break;
        }
        default:
            vm_set_err(vm, "unsupported opcode");
            return -1;
        }
        if (vm->err) return -1;
    }
    return 0;
}

static MiniJvmStats s_stats;

} // namespace

// ================= public entry =================
int minijvm_run_class(const uint8_t* class_data, uint32_t len,
                      void (*out)(const char*, void*), void* ud,
                      int argc, const char* const* argv) {
    if (!class_data || len < 24) return -1;
    VM* vm = (VM*)kalloc(sizeof(VM));
    if (!vm) return -1;
    memset(vm, 0, sizeof(VM));
    vm->out = out;
    vm->ud = ud;
    vm->free_head = -1;
    vm->rng = 0x9E3779B97F4A7C15ULL;
    vm->locals = (Val*)kalloc(sizeof(Val) * (size_t)MAX_FRAMES * MAX_LOCALS);
    if (!vm->locals) { kfree(vm); return -1; }
    memset(vm->locals, 0, sizeof(Val) * (size_t)MAX_FRAMES * MAX_LOCALS);
    for (int i = 0; i < MAX_OBJS; i++) vm->objs[i].kind = OBJ_FREE;
    vm->vfs = g_minijvm_vfs_hooks;
    vm->guard_n = 0;

    int rc = 0;
    do {
        if (!parse_class(class_data, len, &vm->cls)) { rc = -1; break; }
        if (vm->cls.clinit_index >= 0) {
            vm->depth = 0;
            vm->sp = 0;
            vm->frames[0].method = vm->cls.clinit_index;
            vm->frames[0].pc = 0;
            vm->frames[0].stack_base = 0;
            vm->depth = 1;
            rc = run_method(vm, vm->cls.clinit_index);
            if (rc == -2) rc = -1;
            if (rc != 0 || vm->err) { rc = rc ? rc : -1; break; }
            vm->cls.clinit_done = true;
        }
        int mi = vm->cls.main_index;
        if (mi < 0) { vm_set_err(vm, "no main([Ljava/lang/String;)V method"); rc = -1; break; }
        vm->depth = 0;
        vm->sp = 0;
        vm->frames[0].method = mi;
        vm->frames[0].pc = 0;
        vm->frames[0].stack_base = 0;
        vm->depth = 1;
        if (argc > 0 && argv) {
            Obj* a = vm_new_obj(vm, OBJ_STRARR);
            if (!a) { rc = -1; break; }
            a->len = argc;
            a->cap = argc;
            a->arr = (int32_t*)kalloc((size_t)argc * sizeof(int32_t) + 4);
            if (!a->arr) { rc = -1; break; }
            for (int i = 0; i < argc; i++) {
                int64_t s = mk_stringz(vm, argv[i]);
                a->arr[i] = (s >= 0) ? i32(s) : -1;
            }
            vm_push(vm, V_REF((int64_t)(a - vm->objs)));
        } else {
            vm_push(vm, V_NULL());
        }
        rc = run_method(vm, mi);
        if (rc == -2) {
            Obj* e = vm_obj_at(vm, vm->exc_ref);
            const char* cls = e ? obj_class_name(vm, e) : "Throwable";
            char line[192];
            const char* msg = 0;
            if (e && e->flds && e->flds[0] >= 0) {
                Obj* ms = vm_obj_at(vm, e->flds[0]);
                if (ms && ms->kind == OBJ_STRING) msg = ms->str;
            }
            if (msg) ksprintf(line, sizeof(line), "Exception in thread \"main\" %s: %s", cls, msg);
            else ksprintf(line, sizeof(line), "Exception in thread \"main\" %s", cls);
            if (vm->out) vm->out(line, vm->ud);
            rc = -1;
        }
        if (vm->done && rc == 0) rc = vm->exit_code;
        if (vm->err) rc = rc ? rc : -1;
    } while (0);

    s_stats.gc_runs = vm->gc_runs;
    s_stats.objects_used = vm->obj_count;
    s_stats.max_frames = vm->max_depth;

    kfree(vm->locals);
    kfree(vm);
    return rc;
}

void minijvm_set_vfs(const MiniJvmVfs* hooks) { g_minijvm_vfs_hooks = hooks; }

void minijvm_stats(MiniJvmStats* out) {
    if (out) *out = s_stats;
}

} // namespace nefu
