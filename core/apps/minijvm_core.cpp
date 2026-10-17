// nefuOS mini JVM - core helpers: VM ops, GC, strings, throwables, class
// parsing, call/exception plumbing.  See minijvm.h for the overall scope.
#include "minijvm_internal.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace {

// ================= VM helpers =================
static char* dup_str(const char* s, int n) {
    char* d = (char*)kalloc((size_t)n + 1);
    if (!d) return 0;
    memcpy(d, s, (size_t)n);
    d[n] = 0;
    return d;
}

static bool inst_field_is_ref(VM* vm, Obj* o, int slot) {
    if (o->cls && is_throwable_owner(o->cls)) return slot < 2;   // message, cause
    for (int i = 0; i < vm->cls.field_count; i++) {
        if (vm->cls.fields[i].is_static) continue;
        if (vm->cls.fields[i].slot == slot)
            return desc_is_ref(vm->cls.fields[i].desc);
    }
    return false;
}

static void gc_mark(VM* vm, int64_t idx);
static void gc_mark_val(VM* vm, Val v) {
    if (v.kind == K_REF && v.v >= 0 && v.v < vm->obj_count) gc_mark(vm, v.v);
}

static void gc_mark(VM* vm, int64_t idx) {
    if (idx < 0 || idx >= vm->obj_count) return;
    Obj* o = &vm->objs[idx];
    if (o->kind == OBJ_FREE || o->mark) return;
    o->mark = 1;
    switch (o->kind) {
    case OBJ_STRARR:
    case OBJ_ARR:
    case OBJ_SET:
        for (int i = 0; i < o->len; i++)
            if (o->arr[i] >= 0) gc_mark(vm, o->arr[i]);
        break;
    case OBJ_MAP:
        for (int i = 0; i < o->len; i++) {
            if (o->arr[2 * i] >= 0) gc_mark(vm, o->arr[2 * i]);
            if (o->arr[2 * i + 1] >= 0) gc_mark(vm, o->arr[2 * i + 1]);
        }
        break;
    case OBJ_INST:
        for (int i = 0; i < o->fld_count; i++)
            if (inst_field_is_ref(vm, o, i) && o->flds && o->flds[i] >= 0)
                gc_mark(vm, o->flds[i]);
        break;
    case OBJ_ITR:
    case OBJ_STREAM:
        if (o->flds && o->flds[0] >= 0) gc_mark(vm, o->flds[0]);
        break;
    default:
        break;
    }
}

static void obj_free_resources(Obj* o) {
    if (o->cls) { kfree(o->cls); o->cls = 0; }
    if (o->str) { kfree(o->str); o->str = 0; }
    if (o->arr) { kfree(o->arr); o->arr = 0; }
    if (o->larr) { kfree(o->larr); o->larr = 0; }
    if (o->flds) { kfree(o->flds); o->flds = 0; }
    o->kind = OBJ_FREE;
    o->mark = 0;
    o->len = 0; o->cap = 0; o->fld_count = 0;
}

} // namespace

// ================= VM helpers =================
void vm_set_err(VM* vm, const char* msg) {
    if (!vm->err) {
        vm->err = 1;
        int n = ksprintf(vm->errbuf, sizeof(vm->errbuf), "minijvm: %s", msg);
        if (vm->out && n > 0) vm->out(vm->errbuf, vm->ud);
    }
}
void vm_emit(VM* vm, const char* s) {
    if (vm->out && s && s[0]) vm->out(s, vm->ud);
}
void vm_emit_int(VM* vm, int64_t v) {
    char b[40];
    int n = ksprintf(b, sizeof(b), "%lld", (long long)v);
    if (n > 0) vm_emit(vm, b);
}
void vm_emit_char(VM* vm, int c) {
    char b[2];
    b[0] = (char)(c & 0xFF);
    b[1] = 0;
    if (b[0]) vm_emit(vm, b);
}
void format_double_into(char* out, int cap, double d) {
    if (mj_is_nan(d)) { strncpy(out, "NaN", (size_t)cap - 1); return; }
    if (mj_is_inf(d)) { strncpy(out, (mj_d_to_bits(d) & 0x8000000000000000ULL) ? "-Infinity" : "Infinity", (size_t)cap - 1); return; }
    bool neg = d < 0.0;
    if (d == 0.0) { strncpy(out, neg ? "-0.0" : "0.0", (size_t)cap - 1); return; }
    double ad = neg ? -d : d;
    if (ad >= 1e12 || ad < 1e-4) {
        int e = 0;
        double m = ad;
        while (m >= 10.0) { m /= 10.0; e++; }
        while (m < 1.0 && e > -300) { m *= 10.0; e--; }
        long long ip = (long long)m;
        long long fp = (long long)((m - (double)ip) * 1e6 + 0.5);
        if (fp >= 1000000) { fp = 0; ip++; }
        char b[64];
        if (neg) ksprintf(b, sizeof(b), "-%lld.%06lldE%d", ip, fp, e);
        else     ksprintf(b, sizeof(b), "%lld.%06lldE%d", ip, fp, e);
        strncpy(out, b, (size_t)cap - 1);
        return;
    }
    long long ip = (long long)ad;
    double frac = ad - (double)ip;
    long long fp = (long long)(frac * 1e6 + 0.5);
    if (fp >= 1000000) { fp = 0; ip++; }
    char num[40];
    ksprintf(num, sizeof(num), "%lld.%06lld", ip, fp);
    int n = (int)strlen(num);
    while (n > 0 && num[n - 1] == '0') { num[n - 1] = 0; n--; }
    if (n > 0 && num[n - 1] == '.') { num[n] = '0'; num[n + 1] = 0; }
    if (neg) ksprintf(out, cap, "-%s", num);
    else     ksprintf(out, cap, "%s", num);
}
void vm_emit_f64(VM* vm, int64_t bits) {
    char out[48];
    format_double_into(out, sizeof(out), mj_d_from_bits(bits));
    vm_emit(vm, out);
}

void vm_push(VM* vm, Val v) {
    if (vm->sp >= MAX_STACK) { vm_set_err(vm, "operand stack overflow"); return; }
    vm->stack[vm->sp++] = v;
}
Val vm_pop(VM* vm) {
    if (vm->sp <= 0) { vm_set_err(vm, "operand stack underflow"); Val v = V_INT(0); return v; }
    return vm->stack[--vm->sp];
}
Val vm_peek(VM* vm, int n) {
    if (vm->sp - n <= 0) { Val v = V_INT(0); return v; }
    return vm->stack[vm->sp - n];
}

// ================= GC =================
void vm_gc(VM* vm) {
    for (int d = 0; d < vm->depth; d++) {
        Val* L = vm->locals + (size_t)d * MAX_LOCALS;
        for (int i = 0; i < MAX_LOCALS; i++) gc_mark_val(vm, L[i]);
    }
    for (int i = 0; i < vm->sp; i++) gc_mark_val(vm, vm->stack[i]);
    for (int i = 0; i < MAX_STATICS; i++) gc_mark_val(vm, vm->cls.statics[i]);
    for (int i = 0; i < vm->guard_n; i++) gc_mark(vm, vm->guards[i]);
    if (vm->has_exc && vm->exc_ref >= 0) gc_mark(vm, vm->exc_ref);
    vm->free_head = -1;
    for (int i = 0; i < vm->obj_count; i++) {
        Obj* o = &vm->objs[i];
        if (o->kind == OBJ_FREE) { o->next = vm->free_head; vm->free_head = i; continue; }
        if (!o->mark) { obj_free_resources(o); o->next = vm->free_head; vm->free_head = i; }
        else o->mark = 0;
    }
    vm->gc_runs++;
}

Obj* vm_new_obj(VM* vm, uint8_t kind) {
    if (vm->obj_count >= MAX_OBJS && vm->free_head < 0) vm_gc(vm);
    int idx;
    if (vm->free_head >= 0) {
        idx = vm->free_head;
        vm->free_head = vm->objs[idx].next;
    } else {
        if (vm->obj_count >= MAX_OBJS) { vm_set_err(vm, "object table full"); return 0; }
        idx = vm->obj_count++;
    }
    Obj* o = &vm->objs[idx];
    memset(o, 0, sizeof(Obj));
    o->kind = kind;
    o->next = -1;
    return o;
}
Obj* vm_obj_at(VM* vm, int64_t idx) {
    if (idx < 0 || idx >= vm->obj_count) return 0;
    Obj* o = &vm->objs[idx];
    if (o->kind == OBJ_FREE) return 0;
    return o;
}
void vm_guard(VM* vm, int64_t idx) {
    if (idx >= 0 && vm->guard_n < 16) vm->guards[vm->guard_n++] = idx;
}

// ================= strings =================
int64_t mk_string(VM* vm, const char* s, int len) {
    Obj* o = vm_new_obj(vm, OBJ_STRING);
    if (!o) return -1;
    vm_guard(vm, (int64_t)(o - vm->objs));
    o->len = len;
    o->str = (char*)kalloc((size_t)len + 1);
    if (!o->str) { o->len = 0; return -1; }
    if (len > 0) memcpy(o->str, s, (size_t)len);
    o->str[len] = 0;
    return (int64_t)(o - vm->objs);
}
int64_t mk_stringz(VM* vm, const char* s) {
    return mk_string(vm, s, (int)strlen(s));
}
const char* obj_str(Obj* o) { return (o && o->str) ? o->str : ""; }
int64_t str_hash(VM* vm, int64_t ref) {
    Obj* o = vm_obj_at(vm, ref);
    if (!o || o->kind != OBJ_STRING) return 0;
    int64_t h = 0;
    for (int i = 0; i < o->len; i++) h = h * 31 + (uint8_t)o->str[i];
    return h;
}
bool str_equals(VM* vm, int64_t a, int64_t b) {
    Obj* oa = vm_obj_at(vm, a);
    Obj* ob = vm_obj_at(vm, b);
    if (!oa || !ob) return false;
    if (oa->kind != OBJ_STRING || ob->kind != OBJ_STRING) return false;
    if (oa->len != ob->len) return false;
    return memcmp(oa->str, ob->str, (size_t)oa->len) == 0;
}

// ================= boxes =================
Val box_value(VM* vm, char tag, int64_t bits) {
    Obj* o = vm_new_obj(vm, OBJ_BOX);
    if (!o) return V_NULL();
    vm_guard(vm, (int64_t)(o - vm->objs));
    o->cls = (char*)kalloc(2);
    if (o->cls) { o->cls[0] = tag; o->cls[1] = 0; }
    o->val = bits;
    return V_REF((int64_t)(o - vm->objs));
}

// ================= throwable =================
bool is_throwable_owner(const char* o) {
    static const char* known[] = {
        "java/lang/Throwable", "java/lang/Error",
        "java/lang/Exception", "java/lang/RuntimeException",
        "java/lang/NullPointerException", "java/lang/ArithmeticException",
        "java/lang/ArrayIndexOutOfBoundsException", "java/lang/StringIndexOutOfBoundsException",
        "java/lang/IndexOutOfBoundsException", "java/lang/IllegalArgumentException",
        "java/lang/NumberFormatException", "java/lang/ClassCastException",
        "java/lang/IllegalStateException", "java/io/IOException",
        "java/io/FileNotFoundException", "java/lang/NegativeArraySizeException",
        "java/lang/NoSuchFieldError", "java/lang/NoSuchElementException"
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (strcmp(o, known[i]) == 0) return true;
    return false;
}
static bool ends_with(const char* s, const char* suf) {
    size_t n = strlen(s), m = strlen(suf);
    if (n < m) return false;
    return memcmp(s + n - m, suf, m) == 0;
}
bool is_throwable_like(const char* o) {
    if (!o || !o[0]) return false;
    if (is_throwable_owner(o)) return true;
    return ends_with(o, "Exception") || ends_with(o, "Error") || ends_with(o, "Throwable");
}
static bool is_exception_subclass(const char* o) {
    if (!o[0]) return false;
    if (strcmp(o, "java/lang/Exception") == 0) return true;
    if (strcmp(o, "java/lang/Throwable") == 0 || strcmp(o, "java/lang/Error") == 0) return false;
    return true;
}
static bool is_runtime_exc(const char* o) {
    if (!o[0]) return false;
    if (strcmp(o, "java/lang/RuntimeException") == 0) return true;
    if (is_throwable_owner(o)) return false;
    return true;
}
bool is_throwable_match(const char* o, const char* c) {
    if (!o[0]) return false;
    if (strcmp(o, c) == 0) return true;
    if (strcmp(c, "java/lang/Throwable") == 0)
        return is_throwable_like(o) || strcmp(o, "java/lang/Exception") == 0 ||
               strcmp(o, "java/lang/RuntimeException") == 0;
    if (strcmp(c, "java/lang/Exception") == 0) return is_exception_subclass(o);
    if (strcmp(c, "java/lang/RuntimeException") == 0) return is_runtime_exc(o);
    if (strcmp(c, "java/lang/Error") == 0) return strcmp(o, "java/lang/Error") == 0;
    if (strcmp(c, "java/lang/IndexOutOfBoundsException") == 0)
        return strcmp(o, "java/lang/ArrayIndexOutOfBoundsException") == 0 ||
               strcmp(o, "java/lang/StringIndexOutOfBoundsException") == 0;
    if (strcmp(c, "java/lang/IllegalArgumentException") == 0)
        return strcmp(o, "java/lang/NumberFormatException") == 0 ||
               strcmp(o, "java/lang/IllegalArgumentException") == 0;
    if (strcmp(c, "java/io/IOException") == 0)
        return strcmp(o, "java/io/FileNotFoundException") == 0 ||
               strcmp(o, "java/io/IOException") == 0;
    return false;
}

void vm_throw(VM* vm, const char* cls, const char* msg) {
    Obj* o = vm_new_obj(vm, OBJ_INST);
    if (!o) { vm_set_err(vm, "out of objects while throwing"); return; }
    vm_guard(vm, (int64_t)(o - vm->objs));
    o->cls = (char*)kalloc((size_t)strlen(cls) + 1);
    if (o->cls) strcpy(o->cls, cls);
    o->fld_count = 2;
    o->flds = (int64_t*)kalloc(2 * sizeof(int64_t));
    if (o->flds) { o->flds[0] = -1; o->flds[1] = -1; }
    vm->exc_ref = (int64_t)(o - vm->objs);
    vm->has_exc = true;
    if (msg && o->flds) o->flds[0] = mk_stringz(vm, msg);
}

const char* obj_class_name(VM* vm, Obj* o) {
    (void)vm;
    if (!o) return "";
    switch (o->kind) {
    case OBJ_STRING: return "java/lang/String";
    case OBJ_INTARR: return "[I";
    case OBJ_LONGARR: return "[J";
    case OBJ_STRARR: return "[Ljava/lang/Object;";
    case OBJ_PRINTSTREAM: return "java/io/PrintStream";
    case OBJ_STRBUF: return "java/lang/StringBuilder";
    case OBJ_ARR: return "java/util/ArrayList";
    case OBJ_MAP: return "java/util/HashMap";
    case OBJ_SET: return "java/util/HashSet";
    case OBJ_ITR: return "java/util/Iterator";
    case OBJ_CLASS: return "java/lang/Class";
    case OBJ_FILE: return "java/io/File";
    case OBJ_STREAM: return "java/io/InputStream";
    default: return (o->cls && o->cls[0]) ? o->cls : "java/lang/Object";
    }
}

bool type_compatible(VM* vm, Obj* o, const char* target) {
    (void)vm;
    if (!o) return false;
    if (strcmp(target, "java/lang/Object") == 0) return true;
    if (strcmp(target, "java/lang/Cloneable") == 0 || strcmp(target, "java/io/Serializable") == 0)
        return o->kind == OBJ_INTARR || o->kind == OBJ_LONGARR || o->kind == OBJ_STRARR || o->kind == OBJ_INST;
    if (o->kind == OBJ_STRING)
        return strcmp(target, "java/lang/String") == 0 || strcmp(target, "java/lang/CharSequence") == 0 ||
               strcmp(target, "java/lang/Comparable") == 0;
    if (o->kind == OBJ_ARR)
        return strcmp(target, "java/util/ArrayList") == 0 || strcmp(target, "java/util/List") == 0 ||
               strcmp(target, "java/util/Collection") == 0 || strcmp(target, "java/lang/Iterable") == 0 ||
               strcmp(target, "java/util/AbstractList") == 0 || strcmp(target, "java/util/AbstractCollection") == 0;
    if (o->kind == OBJ_MAP)
        return strcmp(target, "java/util/HashMap") == 0 || strcmp(target, "java/util/Map") == 0 ||
               strcmp(target, "java/util/AbstractMap") == 0;
    if (o->kind == OBJ_SET)
        return strcmp(target, "java/util/HashSet") == 0 || strcmp(target, "java/util/Set") == 0 ||
               strcmp(target, "java/util/Collection") == 0 || strcmp(target, "java/lang/Iterable") == 0;
    if (o->kind == OBJ_ITR) return strcmp(target, "java/util/Iterator") == 0;
    if (o->kind == OBJ_INST)
        return is_throwable_match(o->cls ? o->cls : "", target) ||
               strcmp(o->cls ? o->cls : "", target) == 0;
    if (o->kind == OBJ_BOX)
        return strcmp(target, "java/lang/Number") == 0 || strcmp(target, "java/lang/Comparable") == 0;
    if (o->kind == OBJ_CLASS) return strcmp(target, "java/lang/Class") == 0;
    if (o->kind == OBJ_FILE) return strcmp(target, "java/io/File") == 0;
    if (o->kind == OBJ_STREAM)
        return strcmp(target, "java/io/InputStream") == 0 || strcmp(target, "java/io/OutputStream") == 0 ||
               strcmp(target, "java/io/Closeable") == 0 || strcmp(target, "java/io/Flushable") == 0;
    return true;
}

// ================= descriptor / lookup helpers =================
bool desc_is_ref(const char* d) {
    return d && (d[0] == 'L' || d[0] == '[');
}
int desc_arg_slots(const char* d) {
    if (!d || d[0] != '(') return 0;
    const char* q = d + 1;
    int n = 0;
    while (*q && *q != ')') {
        if (*q == '[') { q++; continue; }
        if (*q == 'L') { n++; while (*q && *q != ';') q++; }
        else n++;
        if (*q) q++;
    }
    return n;
}
int find_method(ClassInfo* c, const char* name, const char* desc) {
    for (int i = 0; i < c->method_count; i++) {
        if (strcmp(c->methods[i].name, name) == 0 && strcmp(c->methods[i].desc, desc) == 0)
            return i;
    }
    return -1;
}

// ================= class parsing =================
const char* cp_utf8(ClassInfo* c, uint16_t idx) {
    if (idx == 0 || idx >= c->cp_count) return "";
    return c->cp[idx].utf8 ? c->cp[idx].utf8 : "";
}
const char* cp_class_name(ClassInfo* c, uint16_t idx) {
    if (idx == 0 || idx >= c->cp_count) return "";
    CpEntry* e = &c->cp[idx];
    if (e->tag != 7) return "";
    return cp_utf8(c, e->a);
}
const char* cp_name(ClassInfo* c, uint16_t nat_idx) {
    CpEntry* e = (nat_idx < c->cp_count) ? &c->cp[nat_idx] : 0;
    if (!e || e->tag != 12) return "";
    return cp_utf8(c, e->a);
}
const char* cp_desc(ClassInfo* c, uint16_t nat_idx) {
    CpEntry* e = (nat_idx < c->cp_count) ? &c->cp[nat_idx] : 0;
    if (!e || e->tag != 12) return "";
    return cp_utf8(c, e->b);
}

static bool skip_attrs(const uint8_t* p, uint32_t len, uint32_t* pos) {
    if (*pos + 2 > len) return false;
    uint16_t n = rd_u2(p + *pos); *pos += 2;
    for (uint16_t i = 0; i < n; i++) {
        if (*pos + 6 > len) return false;
        uint32_t alen = rd_u4(p + *pos + 2);
        *pos += 6;
        if (*pos + alen > len) return false;
        *pos += alen;
    }
    return true;
}

bool parse_class(const uint8_t* p, uint32_t len, ClassInfo* c) {
    if (!p || len < 24) return false;
    if (rd_u4(p) != 0xCAFEBABEu) return false;
    uint32_t pos = 8;
    c->cp_count = rd_u2(p + pos); pos += 2;
    if (c->cp_count < 1 || c->cp_count > MAX_CP) return false;
    memset(c->cp, 0, sizeof(CpEntry) * MAX_CP);
    for (uint16_t i = 1; i < c->cp_count; i++) {
        CpEntry* e = &c->cp[i];
        if (pos + 1 > len) return false;
        e->tag = rd_u1(p + pos); pos += 1;
        switch (e->tag) {
        case 1: {
            if (pos + 2 > len) return false;
            uint16_t ul = rd_u2(p + pos); pos += 2;
            if (pos + ul > len) return false;
            e->utf8 = dup_str((const char*)p + pos, ul);
            pos += ul;
            break;
        }
        case 3: case 4:
            if (pos + 4 > len) return false;
            e->ival = rd_s4(p + pos); pos += 4;
            break;
        case 5: case 6:
            if (pos + 8 > len) return false;
            e->lval = (int64_t)rd_u4(p + pos) << 32 | rd_u4(p + pos + 4);
            pos += 8;
            i++;
            break;
        case 7: case 8: case 16: case 19: case 20:
            if (pos + 2 > len) return false;
            e->a = rd_u2(p + pos); pos += 2;
            break;
        case 9: case 10: case 11: case 12:
            if (pos + 4 > len) return false;
            e->a = rd_u2(p + pos); e->b = rd_u2(p + pos + 2); pos += 4;
            break;
        case 15:
            if (pos + 3 > len) return false;
            e->a = rd_u1(p + pos); e->b = rd_u2(p + pos + 1); pos += 3;
            break;
        case 17: case 18:
            if (pos + 4 > len) return false;
            e->a = rd_u2(p + pos); e->b = rd_u2(p + pos + 2); pos += 4;
            break;
        default:
            return false;
        }
    }
    if (pos + 6 > len) return false;
    pos += 2;
    uint16_t this_idx = rd_u2(p + pos); pos += 2;
    pos += 2;
    c->this_name = dup_str(cp_class_name(c, this_idx), (int)strlen(cp_class_name(c, this_idx)));
    if (pos + 2 > len) return false;
    uint16_t if_count = rd_u2(p + pos); pos += 2;
    if (pos + (uint32_t)if_count * 2 > len) return false;
    pos += (uint32_t)if_count * 2;
    if (pos + 2 > len) return false;
    uint16_t fcount = rd_u2(p + pos); pos += 2;
    if (fcount > MAX_FIELDS) return false;
    c->field_count = (int)fcount;
    int statics_slot = 0;
    int inst_slot = 0;
    for (uint16_t i = 0; i < fcount; i++) {
        if (pos + 6 > len) return false;
        uint16_t acc = rd_u2(p + pos);
        uint16_t nidx = rd_u2(p + pos + 2);
        uint16_t didx = rd_u2(p + pos + 4);
        pos += 6;
        FieldInfo* f = &c->fields[i];
        f->name = dup_str(cp_utf8(c, nidx), (int)strlen(cp_utf8(c, nidx)));
        f->desc = dup_str(cp_utf8(c, didx), (int)strlen(cp_utf8(c, didx)));
        f->is_static = (acc & 0x0008) != 0;
        f->slot = -1;
        if (f->is_static) {
            if (statics_slot >= MAX_STATICS) return false;
            f->slot = statics_slot;
            c->statics[statics_slot] = desc_is_ref(f->desc) ? V_NULL() : V_INT(0);
            statics_slot++;
        } else {
            f->slot = inst_slot++;
        }
        if (!skip_attrs(p, len, &pos)) return false;
    }
    c->inst_field_count = inst_slot;
    if (pos + 2 > len) return false;
    uint16_t mcount = rd_u2(p + pos); pos += 2;
    if (mcount > MAX_METHODS) return false;
    c->method_count = (int)mcount;
    c->main_index = -1;
    c->clinit_index = -1;
    for (uint16_t i = 0; i < mcount; i++) {
        if (pos + 6 > len) return false;
        uint16_t acc = rd_u2(p + pos);
        uint16_t nidx = rd_u2(p + pos + 2);
        uint16_t didx = rd_u2(p + pos + 4);
        pos += 6;
        MethodInfo* m = &c->methods[i];
        m->name = dup_str(cp_utf8(c, nidx), (int)strlen(cp_utf8(c, nidx)));
        m->desc = dup_str(cp_utf8(c, didx), (int)strlen(cp_utf8(c, didx)));
        m->is_static = (acc & 0x0008) != 0;
        m->max_stack = 0; m->max_locals = 0; m->code_len = 0; m->code = 0;
        m->exc_count = 0;
        if (pos + 2 > len) return false;
        uint16_t acount = rd_u2(p + pos); pos += 2;
        for (uint16_t a = 0; a < acount; a++) {
            if (pos + 6 > len) return false;
            uint16_t aname = rd_u2(p + pos);
            uint32_t alen = rd_u4(p + pos + 2);
            if (strcmp(cp_utf8(c, aname), "Code") == 0) {
                if (pos + 6 + alen > len) return false;
                const uint8_t* cp0 = p + pos + 6;
                m->max_stack = rd_u2(cp0);
                m->max_locals = rd_u2(cp0 + 2);
                uint32_t clen = rd_u4(cp0 + 4);
                if (clen > MAX_CODE) return false;
                m->code = (uint8_t*)kalloc(clen ? clen : 1);
                if (!m->code) return false;
                memcpy(m->code, cp0 + 8, clen);
                m->code_len = clen;
                uint32_t ep = 8 + clen;
                if (ep + 2 > alen) return false;
                uint16_t et = rd_u2(cp0 + ep); ep += 2;
                if (et > MAX_EXC) return false;
                m->exc_count = (int)et;
                for (uint16_t x = 0; x < et; x++) {
                    if (ep + 8 > alen) return false;
                    ExcInfo* e = &m->exc[x];
                    e->start = rd_u2(cp0 + ep);
                    e->end = rd_u2(cp0 + ep + 2);
                    e->handler = rd_u2(cp0 + ep + 4);
                    e->catch_type = rd_u2(cp0 + ep + 6);
                    ep += 8;
                }
            }
            pos += 6 + alen;
        }
        if (strcmp(m->name, "main") == 0 && strcmp(m->desc, "([Ljava/lang/String;)V") == 0 && m->is_static)
            c->main_index = (int)i;
        if (strcmp(m->name, "<clinit>") == 0 && strcmp(m->desc, "()V") == 0 && m->is_static)
            c->clinit_index = (int)i;
    }
    return true;
}

// ================= numeric conversion helpers =================
int32_t f2i_sat(double d) {
    if (mj_is_nan(d)) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return (int32_t)0x80000000u;
    return (int32_t)d;
}
int64_t f2l_sat(double d) {
    if (mj_is_nan(d)) return 0;
    if (d >= 9223372036854775807.0) return 0x7FFFFFFFFFFFFFFFLL;
    if (d <= -9223372036854775808.0) return (int64_t)0x8000000000000000ULL;
    return (int64_t)d;
}

// ================= call / exception plumbing =================
int call_method(VM* vm, int mi, int nargs, bool has_this) {
    if (vm->depth >= MAX_FRAMES) { vm_set_err(vm, "call stack overflow"); return -1; }
    Frame* f = &vm->frames[vm->depth];
    f->method = mi;
    f->pc = 0;
    f->stack_base = vm->sp - nargs - (has_this ? 1 : 0);
    if (f->stack_base < 0) { vm_set_err(vm, "bad frame base"); return -1; }
    Val* L = vm->locals + (size_t)vm->depth * MAX_LOCALS;
    memset(L, 0, sizeof(Val) * MAX_LOCALS);
    int slot = 0;
    if (has_this) L[slot++] = vm->stack[f->stack_base];
    for (int i = 0; i < nargs; i++) L[slot++] = vm->stack[f->stack_base + (has_this ? 1 : 0) + i];
    vm->depth++;
    if (vm->depth > vm->max_depth) vm->max_depth = vm->depth;
    return 0;
}

int find_handler(VM* vm, MethodInfo* m, int pc, const char* ecls) {
    for (int i = 0; i < m->exc_count; i++) {
        ExcInfo* x = &m->exc[i];
        if (pc >= x->start && pc < x->end) {
            if (x->catch_type == 0) return x->handler;
            const char* ct = cp_class_name(&vm->cls, x->catch_type);
            if (is_throwable_match(ecls, ct)) return x->handler;
        }
    }
    return -1;
}

int propagate_exc(VM* vm) {
    if (!vm->has_exc) return 0;
    if (vm->depth <= 0) return -2;
    Frame* f = &vm->frames[vm->depth - 1];
    MethodInfo* m = &vm->cls.methods[f->method];
    Obj* e = vm_obj_at(vm, vm->exc_ref);
    const char* ecls = e ? obj_class_name(vm, e) : "";
    // The exception table covers [start,end) of *instruction* pcs; f->pc has
    // already advanced past the faulting instruction (athrow is 1 byte, so
    // pc-1 is the throwing instruction).
    int hpc = find_handler(vm, m, f->pc - 1, ecls);
    if (hpc >= 0) {
        vm->sp = f->stack_base;
        vm_push(vm, V_REF(vm->exc_ref));
        f->pc = hpc;
        vm->has_exc = false;
        return 0;
    }
    vm->depth--;
    if (vm->depth <= 0) return -2;
    vm->sp = vm->frames[vm->depth].stack_base;
    return -2;
}

} // namespace nefu
