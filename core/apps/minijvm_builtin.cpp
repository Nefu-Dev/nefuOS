// nefuOS mini JVM - built-in class dispatch (java.lang.* / java.util.* /
// java.io.* core methods implemented natively).  Returns 1 when handled,
// 0 when the owner is unknown to the VM.  Part 1: helpers + String +
// StringBuilder + Math + boxed primitives + System.
#include "minijvm_internal.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace {

// ---------------- boxed helpers ----------------
static bool is_box_tag(Obj* o, char tag) {
    return o && o->kind == OBJ_BOX && o->cls && o->cls[0] == tag;
}
static int64_t unbox_bits(Obj* o) { return o ? o->val : 0; }
static int64_t box_int(VM* vm, int32_t v) { return box_value(vm, 'I', v).v; }
static int64_t box_long(VM* vm, int64_t v) { return box_value(vm, 'J', v).v; }
static int64_t box_boolean(VM* vm, int v) { return box_value(vm, 'Z', v).v; }
static int64_t box_char(VM* vm, int v) { return box_value(vm, 'C', v).v; }
static int64_t box_byte(VM* vm, int v) { return box_value(vm, 'B', v).v; }
static int64_t box_short(VM* vm, int v) { return box_value(vm, 'S', v).v; }
static int64_t box_float(VM* vm, float f) { return box_value(vm, 'F', (int64_t)(uint32_t)mj_f_to_bits(f)).v; }
static int64_t box_double(VM* vm, double d) { return box_value(vm, 'D', mj_d_to_bits(d)).v; }

static int32_t unbox_int(Obj* o) {
    if (!o || o->kind != OBJ_BOX) return 0;
    if (o->cls[0] == 'F') return f2i_sat((double)mj_f_from_bits((uint32_t)o->val));
    if (o->cls[0] == 'D') return f2i_sat(mj_d_from_bits((uint64_t)o->val));
    return i32(o->val);
}
static int64_t unbox_long(Obj* o) {
    if (!o || o->kind != OBJ_BOX) return 0;
    if (o->cls[0] == 'F') return f2l_sat((double)mj_f_from_bits((uint32_t)o->val));
    if (o->cls[0] == 'D') return f2l_sat(mj_d_from_bits((uint64_t)o->val));
    return o->val;
}
static double unbox_double(Obj* o) {
    if (!o || o->kind != OBJ_BOX) return 0.0;
    if (o->cls[0] == 'F') return (double)mj_f_from_bits((uint32_t)o->val);
    if (o->cls[0] == 'D') return mj_d_from_bits((uint64_t)o->val);
    return (double)o->val;
}
static int unbox_boolean(Obj* o) { return (o && o->kind == OBJ_BOX) ? (o->val != 0 ? 1 : 0) : 0; }
static int unbox_char(Obj* o) { return (o && o->kind == OBJ_BOX) ? i32(o->val) : 0; }

// ---------------- string buffer append ----------------
static int64_t sb_append_c(Obj* sb, char c) {
    if (sb->len + 1 > sb->cap) {
        int nc = sb->cap < 4 ? 8 : sb->cap * 2;
        char* ns = (char*)kalloc((size_t)nc + 1);
        if (!ns) return -1;
        if (sb->str) { memcpy(ns, sb->str, (size_t)sb->len); kfree(sb->str); }
        sb->str = ns;
        sb->cap = nc;
    }
    sb->str[sb->len++] = c;
    sb->str[sb->len] = 0;
    return 0;
}
static int64_t sb_append_s(Obj* sb, const char* s, int n) {
    if (sb->len + n > sb->cap) {
        int nc = sb->cap;
        while (nc < sb->len + n) nc = nc < 4 ? 8 : nc * 2;
        char* ns = (char*)kalloc((size_t)nc + 1);
        if (!ns) return -1;
        if (sb->str) { memcpy(ns, sb->str, (size_t)sb->len); kfree(sb->str); }
        sb->str = ns;
        sb->cap = nc;
    }
    if (n > 0 && s) memcpy(sb->str + sb->len, s, (size_t)n);
    sb->len += n;
    sb->str[sb->len] = 0;
    return 0;
}

static int64_t format_double(VM* vm, double d) {
    char b[48];
    format_double_into(b, sizeof(b), d);
    return mk_stringz(vm, b);
}
static int64_t format_double_append(VM* vm, Obj* sb, double d) {
    char b[48];
    format_double_into(b, sizeof(b), d);
    return sb_append_s(sb, b, (int)strlen(b));
}

static int64_t array_to_string(VM* vm, Obj* o) {
    char hdr[48];
    if (o->kind == OBJ_INTARR) strncpy(hdr, "[I@0", 47);
    else if (o->kind == OBJ_LONGARR) strncpy(hdr, "[J@0", 47);
    else strncpy(hdr, "[Ljava.lang.Object;@0", 47);
    return mk_stringz(vm, hdr);
}

// ---------------- to-string (used by StringBuilder / PrintStream) ----------------
int64_t to_string_ref(VM* vm, Val v) {
    if (v.kind == K_NULL) return mk_stringz(vm, "null");
    if (v.kind == K_REF) {
        Obj* o = vm_obj_at(vm, v.v);
        if (!o) return mk_stringz(vm, "null");
        if (o->kind == OBJ_STRING) return v.v;
        if (o->kind == OBJ_BOX) {
            char tag = o->cls[0];
            if (tag == 'Z') return mk_stringz(vm, o->val ? "true" : "false");
            if (tag == 'C') { char b[2]; b[0] = (char)i32(o->val); b[1] = 0; return mk_string(vm, b, 1); }
            if (tag == 'I') { char b[24]; ksprintf(b, sizeof(b), "%d", i32(o->val)); return mk_stringz(vm, b); }
            if (tag == 'J') { char b[28]; ksprintf(b, sizeof(b), "%lld", (long long)o->val); return mk_stringz(vm, b); }
            if (tag == 'B') { char b[12]; ksprintf(b, sizeof(b), "%d", i32(o->val)); return mk_stringz(vm, b); }
            if (tag == 'S') { char b[12]; ksprintf(b, sizeof(b), "%d", i32(o->val)); return mk_stringz(vm, b); }
            if (tag == 'F') return format_double(vm, (double)mj_f_from_bits((uint32_t)o->val));
            if (tag == 'D') return format_double(vm, mj_d_from_bits((uint64_t)o->val));
        }
        if (o->kind == OBJ_INTARR || o->kind == OBJ_LONGARR || o->kind == OBJ_STRARR)
            return array_to_string(vm, o);
        if (o->kind == OBJ_STRBUF) return mk_string(vm, o->str, o->len);
        if (o->kind == OBJ_PRINTSTREAM) return mk_stringz(vm, "java.io.PrintStream@0");
        if (o->kind == OBJ_ARR) return mk_stringz(vm, "java.util.ArrayList@0");
        if (o->kind == OBJ_MAP) return mk_stringz(vm, "java.util.HashMap@0");
        if (o->kind == OBJ_SET) return mk_stringz(vm, "java.util.HashSet@0");
        if (o->kind == OBJ_ITR) return mk_stringz(vm, "java.util.Iterator@0");
        if (o->kind == OBJ_FILE) return mk_stringz(vm, o->cls ? o->cls : "");
        return mk_stringz(vm, o->cls && o->cls[0] ? o->cls : "java.lang.Object");
    }
    if (v.kind == K_F32) return format_double(vm, (double)mj_f_from_bits((uint32_t)v.v));
    if (v.kind == K_F64) return format_double(vm, mj_d_from_bits((uint64_t)v.v));
    if (v.kind == K_LONG) { char b[28]; ksprintf(b, sizeof(b), "%lld", (long long)v.v); return mk_stringz(vm, b); }
    char b[24]; ksprintf(b, sizeof(b), "%d", i32(v.v)); return mk_stringz(vm, b);
}

// ---------------- String ----------------
static Obj* arg_str(VM* vm, int64_t ref) {
    Obj* o = vm_obj_at(vm, ref);
    return (o && o->kind == OBJ_STRING) ? o : 0;
}

// plain ASCII parsers (string objs are NUL-terminated by mk_string)
static bool ends_with_c(const char* s, const char* suf) {
    size_t n = strlen(s), m = strlen(suf);
    if (n < m) return false;
    return memcmp(s + n - m, suf, m) == 0;
}
static int64_t parse_long(const char* s, int len) {
    int i = 0;
    bool neg = false;
    if (i < len && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    int64_t r = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        r = r * 10 + (s[i] - '0');
        if (r > 0x3FFFFFFFFFFFFFFFLL) r = 0x3FFFFFFFFFFFFFFFLL;
        i++;
    }
    return neg ? -r : r;
}
static double parse_double(const char* s, int len) {
    int i = 0;
    bool neg = false;
    if (i < len && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    double ip = 0.0;
    while (i < len && s[i] >= '0' && s[i] <= '9') { ip = ip * 10.0 + (s[i] - '0'); i++; }
    double fp = 0.0;
    if (i < len && s[i] == '.') {
        i++;
        double scale = 0.1;
        while (i < len && s[i] >= '0' && s[i] <= '9') { fp += (s[i] - '0') * scale; scale *= 0.1; i++; }
    }
    double v = ip + fp;
    return neg ? -v : v;
}

static int do_string(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) return 1;   // empty-string ctor is a no-op here
    Obj* s = has_this ? arg_str(vm, vm_peek(vm, desc_arg_slots(d) + 1).v) : 0;
    if (!s) { vm_throw(vm, "java/lang/NullPointerException", "null string"); return 1; }
    if (strcmp(n, "length") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(s->len)); return 1; }
    if (strcmp(n, "isEmpty") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(s->len == 0 ? 1 : 0)); return 1; }
    if (strcmp(n, "charAt") == 0 && strcmp(d, "(I)C") == 0) {
        int i = i32(vm_peek(vm, 1).v);
        if (i < 0 || i >= s->len) { vm_throw(vm, "java/lang/StringIndexOutOfBoundsException", "charAt"); return 1; }
        vm_push(vm, V_INT((uint8_t)s->str[i]));
        return 1;
    }
    if (strcmp(n, "toString") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        vm_push(vm, V_REF(vm_peek(vm, 1).v));
        return 1;
    }
    if (strcmp(n, "equals") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        Obj* a = arg_str(vm, vm_peek(vm, 1).v);
        vm_push(vm, V_INT(a && a->len == s->len && memcmp(s->str, a->str, (size_t)s->len) == 0 ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "hashCode") == 0 && strcmp(d, "()I") == 0) {
        int64_t h = 0;
        for (int i = 0; i < s->len; i++) h = h * 31 + (uint8_t)s->str[i];
        vm_push(vm, V_INT((int64_t)i32(h)));
        return 1;
    }
    if (strcmp(n, "compareTo") == 0 && strcmp(d, "(Ljava/lang/String;)I") == 0) {
        Obj* a = arg_str(vm, vm_peek(vm, 1).v);
        int ml = a ? (s->len < a->len ? s->len : a->len) : 0;
        for (int i = 0; i < ml; i++) {
            if ((uint8_t)s->str[i] != (uint8_t)a->str[i])
                return (vm_push(vm, V_INT((int64_t)((uint8_t)s->str[i] - (uint8_t)a->str[i]))), 1);
        }
        vm_push(vm, V_INT((int64_t)(s->len - (a ? a->len : 0))));
        return 1;
    }
    if ((strcmp(n, "substring") == 0) && (strcmp(d, "(II)Ljava/lang/String;") == 0 || strcmp(d, "(I)Ljava/lang/String;") == 0)) {
        int e = (d[2] == 'I' && d[0] == '(' && d[1] == 'I') ? i32(vm_peek(vm, 1).v) : s->len;
        int b = (d[2] == 'I') ? i32(vm_peek(vm, 2).v) : i32(vm_peek(vm, 1).v);
        if (b < 0 || e > s->len || b > e) { vm_throw(vm, "java/lang/StringIndexOutOfBoundsException", "substring"); return 1; }
        int64_t r = mk_string(vm, s->str + b, e - b);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "startsWith") == 0 && strcmp(d, "(Ljava/lang/String;)Z") == 0) {
        Obj* a = arg_str(vm, vm_peek(vm, 1).v);
        bool ok = a && a->len <= s->len && memcmp(s->str, a->str, (size_t)a->len) == 0;
        vm_push(vm, V_INT(ok ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "endsWith") == 0 && strcmp(d, "(Ljava/lang/String;)Z") == 0) {
        Obj* a = arg_str(vm, vm_peek(vm, 1).v);
        bool ok = a && a->len <= s->len && memcmp(s->str + s->len - a->len, a->str, (size_t)a->len) == 0;
        vm_push(vm, V_INT(ok ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "contains") == 0 && strcmp(d, "(Ljava/lang/CharSequence;)Z") == 0) {
        Obj* a = arg_str(vm, vm_peek(vm, 1).v);
        bool ok = false;
        if (a) {
            for (int i = 0; i + a->len <= s->len; i++)
                if (memcmp(s->str + i, a->str, (size_t)a->len) == 0) { ok = true; break; }
        }
        vm_push(vm, V_INT(ok ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "indexOf") == 0 && (strcmp(d, "(I)I") == 0 || strcmp(d, "(II)I") == 0 || strcmp(d, "(Ljava/lang/String;)I") == 0)) {
        int64_t r = -1;
        if (d[1] == 'I') {
            int ch = (d[2] == 'I') ? i32(vm_peek(vm, 2).v) : i32(vm_peek(vm, 1).v);
            int from = (d[2] == 'I') ? i32(vm_peek(vm, 1).v) : 0;
            for (int i = from; i < s->len; i++)
                if ((uint8_t)s->str[i] == ch) { r = i; break; }
        } else {
            Obj* a = arg_str(vm, vm_peek(vm, 1).v);
            if (a) {
                for (int i = 0; i + a->len <= s->len; i++)
                    if (memcmp(s->str + i, a->str, (size_t)a->len) == 0) { r = i; break; }
            }
        }
        vm_push(vm, V_INT(r));
        return 1;
    }
    if (strcmp(n, "lastIndexOf") == 0 && (strcmp(d, "(I)I") == 0 || strcmp(d, "(II)I") == 0)) {
        int ch = (d[2] == 'I') ? i32(vm_peek(vm, 2).v) : i32(vm_peek(vm, 1).v);
        int from = (d[2] == 'I') ? i32(vm_peek(vm, 1).v) : s->len - 1;
        int64_t r = -1;
        for (int i = from; i >= 0; i--)
            if ((uint8_t)s->str[i] == ch) { r = i; break; }
        vm_push(vm, V_INT(r));
        return 1;
    }
    if (strcmp(n, "toUpperCase") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        char* t = (char*)kalloc((size_t)s->len + 1);
        if (!t) return 1;
        for (int i = 0; i < s->len; i++) {
            char c = s->str[i];
            t[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        t[s->len] = 0;
        int64_t r = mk_string(vm, t, s->len);
        kfree(t);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "toLowerCase") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        char* t = (char*)kalloc((size_t)s->len + 1);
        if (!t) return 1;
        for (int i = 0; i < s->len; i++) {
            char c = s->str[i];
            t[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        t[s->len] = 0;
        int64_t r = mk_string(vm, t, s->len);
        kfree(t);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "trim") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        int b = 0, e = s->len;
        while (b < e && (s->str[b] == ' ' || s->str[b] == '\t' || s->str[b] == '\n')) b++;
        while (e > b && (s->str[e - 1] == ' ' || s->str[e - 1] == '\t' || s->str[e - 1] == '\n')) e--;
        int64_t r = mk_string(vm, s->str + b, e - b);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "replace") == 0 && strcmp(d, "(CC)Ljava/lang/String;") == 0) {
        char nc = (char)i32(vm_peek(vm, 1).v);
        char oc = (char)i32(vm_peek(vm, 2).v);
        char* t = (char*)kalloc((size_t)s->len + 1);
        if (!t) return 1;
        for (int i = 0; i < s->len; i++) t[i] = (s->str[i] == oc) ? nc : s->str[i];
        t[s->len] = 0;
        int64_t r = mk_string(vm, t, s->len);
        kfree(t);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "getBytes") == 0 && strcmp(d, "()[B") == 0) {
        Obj* o = vm_new_obj(vm, OBJ_INTARR);
        if (o) {
            o->len = s->len; o->cap = s->len;
            o->arr = (int32_t*)kalloc((size_t)s->len * sizeof(int32_t) + 4);
            if (o->arr) { for (int i = 0; i < s->len; i++) o->arr[i] = (uint8_t)s->str[i]; }
            vm_push(vm, V_REF((int64_t)(o - vm->objs)));
        }
        return 1;
    }
    return 0;
}

// ---------------- StringBuilder / StringBuffer ----------------
static Obj* arg_sb(VM* vm, int64_t ref) {
    Obj* o = vm_obj_at(vm, ref);
    return (o && o->kind == OBJ_STRBUF) ? o : 0;
}
static int64_t sb_append_val(VM* vm, Obj* sb, Val v) {
    if (v.kind == K_INT) {
        char b[24];
        ksprintf(b, sizeof(b), "%d", i32(v.v));
        return sb_append_s(sb, b, (int)strlen(b));
    }
    if (v.kind == K_LONG) {
        char b[28];
        ksprintf(b, sizeof(b), "%lld", (long long)v.v);
        return sb_append_s(sb, b, (int)strlen(b));
    }
    if (v.kind == K_NULL) return sb_append_s(sb, "null", 4);
    if (v.kind == K_REF) {
        int64_t r = to_string_ref(vm, v);
        Obj* o = (r >= 0) ? vm_obj_at(vm, r) : 0;
        return o ? sb_append_s(sb, o->str, o->len) : sb_append_s(sb, "null", 4);
    }
    if (v.kind == K_F32) return format_double_append(vm, sb, (double)mj_f_from_bits((uint32_t)v.v));
    if (v.kind == K_F64) return format_double_append(vm, sb, mj_d_from_bits((uint64_t)v.v));
    return 0;
}
static int do_stringbuilder(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) return 1;   // ctor: object already created by 'new'
    if (!has_this) {
        return 0;
    }
    Obj* sb = arg_sb(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!sb) { vm_throw(vm, "java/lang/NullPointerException", "null builder"); return 1; }
    if (strcmp(n, "toString") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        int64_t r = mk_string(vm, sb->str, sb->len);
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "length") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(sb->len)); return 1; }
    if (strcmp(n, "capacity") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(sb->cap)); return 1; }
    if (strcmp(n, "append") == 0 && d[0] == '(' && ends_with_c(d, ")Ljava/lang/StringBuilder;")) {
        Val v;
        char t = d[1];
        if (t == 'C') { v.kind = K_INT; v.v = vm_peek(vm, 1).v; char c = (char)i32(v.v); sb_append_c(sb, c); }
        else if (t == 'Z') { v.kind = K_INT; v.v = vm_peek(vm, 1).v; sb_append_s(sb, i32(v.v) ? "true" : "false", i32(v.v) ? 4 : 5); }
        else if (t == 'I' || t == 'B' || t == 'S') { v.kind = K_INT; v.v = i32(vm_peek(vm, 1).v); sb_append_val(vm, sb, v); }
        else if (t == 'J') { v.kind = K_LONG; v.v = vm_peek(vm, 1).v; sb_append_val(vm, sb, v); }
        else if (t == 'F') { v.kind = K_F32; v.v = vm_peek(vm, 1).v; sb_append_val(vm, sb, v); }
        else if (t == 'D') { v.kind = K_F64; v.v = vm_peek(vm, 1).v; sb_append_val(vm, sb, v); }
        else { sb_append_val(vm, sb, vm_peek(vm, 1)); }
        vm_push(vm, V_REF(vm_peek(vm, desc_arg_slots(d) + 1).v));
        return 1;
    }
    if (strcmp(n, "append") == 0 && (strcmp(d, "(Ljava/lang/String;)Ljava/lang/StringBuilder;") == 0 || strcmp(d, "(Ljava/lang/Object;)Ljava/lang/StringBuilder;") == 0)) {
        sb_append_val(vm, sb, vm_peek(vm, 1));
        vm_push(vm, V_REF(vm_peek(vm, desc_arg_slots(d) + 1).v));
        return 1;
    }
    return 0;
}

// ---------------- Math ----------------
static double arg_d(VM* vm, int k) {
    Val v = vm_peek(vm, k);
    if (v.kind == K_F64) return mj_d_from_bits((uint64_t)v.v);
    if (v.kind == K_F32) return (double)mj_f_from_bits((uint32_t)v.v);
    if (v.kind == K_LONG) return (double)v.v;
    return (double)i32(v.v);
}
static int64_t arg_l(VM* vm, int k) {
    Val v = vm_peek(vm, k);
    return (v.kind == K_LONG) ? v.v : (int64_t)i32(v.v);
}
static int do_math(VM* vm, const char* n, const char* d) {
    bool is_int = strcmp(d, "(II)I") == 0 || strcmp(d, "(I)I") == 0 || strcmp(d, "(IJ)J") == 0 || strcmp(d, "(JJ)J") == 0 || strcmp(d, "(J)J") == 0;
    bool is_long = strcmp(d, "(J)J") == 0 || strcmp(d, "(JJ)J") == 0 || strcmp(d, "(IJ)J") == 0;
    bool is_float = strcmp(d, "(FF)F") == 0 || strcmp(d, "(F)F") == 0;
    bool is_double = strcmp(d, "(DD)D") == 0 || strcmp(d, "(D)D") == 0;
    if (strcmp(n, "sqrt") == 0 && is_double) { vm_push(vm, V_F64(mj_sqrt(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "pow") == 0 && is_double) { vm_push(vm, V_F64(mj_pow(arg_d(vm, 2), arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "sin") == 0 && is_double) { vm_push(vm, V_F64(mj_sin(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "cos") == 0 && is_double) { vm_push(vm, V_F64(mj_cos(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "tan") == 0 && is_double) { vm_push(vm, V_F64(mj_tan(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "asin") == 0 && is_double) { vm_push(vm, V_F64(mj_asin(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "acos") == 0 && is_double) { vm_push(vm, V_F64(mj_acos(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "atan") == 0 && is_double) { vm_push(vm, V_F64(mj_atan(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "atan2") == 0 && is_double) { vm_push(vm, V_F64(mj_atan2(arg_d(vm, 2), arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "exp") == 0 && is_double) { vm_push(vm, V_F64(mj_exp(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "log") == 0 && is_double) { vm_push(vm, V_F64(mj_ln(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "log10") == 0 && is_double) { vm_push(vm, V_F64(mj_log10(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "cbrt") == 0 && is_double) { vm_push(vm, V_F64(mj_cbrt(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "floor") == 0 && is_double) { vm_push(vm, V_F64(mj_floor(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "ceil") == 0 && is_double) { vm_push(vm, V_F64(mj_ceil(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "abs") == 0 && is_double) { vm_push(vm, V_F64(mj_abs(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "abs") == 0 && is_float) { float f = (float)arg_d(vm, 1); vm_push(vm, V_F32(f < 0 ? -f : f)); return 1; }
    if (strcmp(n, "abs") == 0 && is_int) { int64_t a = arg_l(vm, 1); vm_push(vm, V_INT(a < 0 ? -a : a)); return 1; }
    if (strcmp(n, "abs") == 0 && is_long) { int64_t a = arg_l(vm, 1); vm_push(vm, V_LONG(a < 0 ? -a : a)); return 1; }
    if (strcmp(n, "max") == 0 && is_double) { vm_push(vm, V_F64(mj_max(arg_d(vm, 1), arg_d(vm, 2)))); return 1; }
    if (strcmp(n, "min") == 0 && is_double) { vm_push(vm, V_F64(mj_min(arg_d(vm, 1), arg_d(vm, 2)))); return 1; }
    if (strcmp(n, "max") == 0 && is_int) { int64_t a = arg_l(vm, 1), b = arg_l(vm, 2); vm_push(vm, V_INT(a > b ? a : b)); return 1; }
    if (strcmp(n, "min") == 0 && is_int) { int64_t a = arg_l(vm, 1), b = arg_l(vm, 2); vm_push(vm, V_INT(a < b ? a : b)); return 1; }
    if (strcmp(n, "max") == 0 && is_long) { int64_t a = arg_l(vm, 1), b = arg_l(vm, 2); vm_push(vm, V_LONG(a > b ? a : b)); return 1; }
    if (strcmp(n, "min") == 0 && is_long) { int64_t a = arg_l(vm, 1), b = arg_l(vm, 2); vm_push(vm, V_LONG(a < b ? a : b)); return 1; }
    if (strcmp(n, "round") == 0 && strcmp(d, "(D)J") == 0) { double a = arg_d(vm, 1); vm_push(vm, V_LONG(mj_floor(a + 0.5))); return 1; }
    if (strcmp(n, "round") == 0 && strcmp(d, "(F)I") == 0) { double a = arg_d(vm, 1); vm_push(vm, V_INT((int64_t)mj_floor(a + 0.5))); return 1; }
    if (strcmp(n, "random") == 0 && strcmp(d, "()D") == 0) {
        vm->rng = vm->rng * 6364136223846793005ULL + 1442695040888963407ULL;
        double r = (double)((vm->rng >> 11) & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
        vm_push(vm, V_F64(r));
        return 1;
    }
    if (strcmp(n, "toRadians") == 0 && is_double) { vm_push(vm, V_F64(mj_deg2rad(arg_d(vm, 1)))); return 1; }
    if (strcmp(n, "toDegrees") == 0 && is_double) { vm_push(vm, V_F64(mj_rad2deg(arg_d(vm, 1)))); return 1; }
    return 0;
}

// ---------------- boxed primitives ----------------
static int do_box(VM* vm, const char* own, const char* n, const char* d, bool has_this) {
    char tag = 0;
    if (strcmp(own, "java/lang/Integer") == 0) tag = 'I';
    else if (strcmp(own, "java/lang/Long") == 0) tag = 'J';
    else if (strcmp(own, "java/lang/Boolean") == 0) tag = 'Z';
    else if (strcmp(own, "java/lang/Character") == 0) tag = 'C';
    else if (strcmp(own, "java/lang/Byte") == 0) tag = 'B';
    else if (strcmp(own, "java/lang/Short") == 0) tag = 'S';
    else if (strcmp(own, "java/lang/Float") == 0) tag = 'F';
    else if (strcmp(own, "java/lang/Double") == 0) tag = 'D';
    if (!tag) return 0;

    if (strcmp(n, "<init>") == 0) {
        // new Integer(5) / new Long(...) / new Double(...) etc.
        int slots = desc_arg_slots(d);
        Obj* o = vm_obj_at(vm, vm_peek(vm, slots + 1).v);
        if (o && o->kind == OBJ_BOX) {
            Val a = vm_peek(vm, 1);
            if (slots == 1 && (d[1] == 'L')) {           // ctor from String
                Obj* s = (a.kind == K_REF) ? vm_obj_at(vm, a.v) : 0;
                int64_t v = 0;
                if (s && s->kind == OBJ_STRING) {
                    if (tag == 'D') v = mj_d_to_bits(parse_double(s->str, s->len));
                    else if (tag == 'F') v = (uint32_t)mj_f_to_bits((float)parse_double(s->str, s->len));
                    else if (tag == 'Z') v = (strcmp(s->str, "true") == 0) ? 1 : 0;
                    else v = parse_long(s->str, s->len);
                }
                o->val = v;
            } else {
                o->val = a.v;
            }
        }
        return 1;
    }

    if (!has_this) {
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(I)Ljava/lang/Integer;") == 0 && tag == 'I') {
            vm_push(vm, V_REF(box_int(vm, i32(vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(J)Ljava/lang/Long;") == 0 && tag == 'J') {
            vm_push(vm, V_REF(box_long(vm, vm_peek(vm, 1).v))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(Z)Ljava/lang/Boolean;") == 0 && tag == 'Z') {
            vm_push(vm, V_REF(box_boolean(vm, i32(vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(C)Ljava/lang/Character;") == 0 && tag == 'C') {
            vm_push(vm, V_REF(box_char(vm, i32(vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(B)Ljava/lang/Byte;") == 0 && tag == 'B') {
            vm_push(vm, V_REF(box_byte(vm, i32(vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(S)Ljava/lang/Short;") == 0 && tag == 'S') {
            vm_push(vm, V_REF(box_short(vm, i32(vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(F)Ljava/lang/Float;") == 0 && tag == 'F') {
            vm_push(vm, V_REF(box_float(vm, mj_f_from_bits((uint32_t)vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(D)Ljava/lang/Double;") == 0 && tag == 'D') {
            vm_push(vm, V_REF(box_double(vm, mj_d_from_bits((uint64_t)vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(Ljava/lang/String;)Ljava/lang/Integer;") == 0 && tag == 'I') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            int32_t v = s ? (int32_t)parse_long(s->str, s->len) : 0;
            vm_push(vm, V_REF(box_int(vm, v))); return 1;
        }
        if (strcmp(n, "valueOf") == 0 && strcmp(d, "(Ljava/lang/String;)Ljava/lang/Long;") == 0 && tag == 'J') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            int64_t v = s ? parse_long(s->str, s->len) : 0;
            vm_push(vm, V_REF(box_long(vm, v))); return 1;
        }
        if (strcmp(n, "parseInt") == 0 && strcmp(d, "(Ljava/lang/String;)I") == 0 && tag == 'I') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            vm_push(vm, V_INT(s ? (int32_t)parse_long(s->str, s->len) : 0)); return 1;
        }
        if (strcmp(n, "parseLong") == 0 && strcmp(d, "(Ljava/lang/String;)J") == 0 && tag == 'J') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            vm_push(vm, V_LONG(s ? parse_long(s->str, s->len) : 0)); return 1;
        }
        if (strcmp(n, "parseBoolean") == 0 && strcmp(d, "(Ljava/lang/String;)Z") == 0 && tag == 'Z') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            vm_push(vm, V_INT(s && s->len == 4 && memcmp(s->str, "true", 4) == 0 ? 1 : 0)); return 1;
        }
        if (strcmp(n, "parseFloat") == 0 && strcmp(d, "(Ljava/lang/String;)F") == 0 && tag == 'F') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            float f = s ? (float)parse_double(s->str, s->len) : 0.0f;
            vm_push(vm, V_F32(f)); return 1;
        }
        if (strcmp(n, "parseDouble") == 0 && strcmp(d, "(Ljava/lang/String;)D") == 0 && tag == 'D') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            double f = s ? parse_double(s->str, s->len) : 0.0;
            vm_push(vm, V_F64(f)); return 1;
        }
        if (strcmp(n, "toString") == 0 && strcmp(d, "(I)Ljava/lang/String;") == 0 && tag == 'I') {
            char b[24]; ksprintf(b, sizeof(b), "%d", i32(vm_peek(vm, 1).v));
            vm_push(vm, V_REF(mk_stringz(vm, b))); return 1;
        }
        if (strcmp(n, "toString") == 0 && strcmp(d, "(J)Ljava/lang/String;") == 0 && tag == 'J') {
            char b[28]; ksprintf(b, sizeof(b), "%lld", (long long)vm_peek(vm, 1).v);
            vm_push(vm, V_REF(mk_stringz(vm, b))); return 1;
        }
        if (strcmp(n, "toString") == 0 && strcmp(d, "(Z)Ljava/lang/String;") == 0 && tag == 'Z') {
            vm_push(vm, V_REF(mk_stringz(vm, i32(vm_peek(vm, 1).v) ? "true" : "false"))); return 1;
        }
        if (strcmp(n, "toString") == 0 && strcmp(d, "(F)Ljava/lang/String;") == 0 && tag == 'F') {
            vm_push(vm, V_REF(format_double(vm, (double)mj_f_from_bits((uint32_t)vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "toString") == 0 && strcmp(d, "(D)Ljava/lang/String;") == 0 && tag == 'D') {
            vm_push(vm, V_REF(format_double(vm, mj_d_from_bits((uint64_t)vm_peek(vm, 1).v)))); return 1;
        }
        if (strcmp(n, "getInteger") == 0 && tag == 'I' && d[0] == '(') {
            Obj* s = arg_str(vm, vm_peek(vm, 1).v);
            int32_t v = s ? (int32_t)parse_long(s->str, s->len) : 0;
            vm_push(vm, V_REF(box_int(vm, v))); return 1;
        }
        return 0;
    }
    // instance methods
    Obj* o = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!o || o->kind != OBJ_BOX) { vm_push(vm, V_INT(0)); return 1; }
    if (strcmp(n, "intValue") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(unbox_int(o))); return 1; }
    if (strcmp(n, "longValue") == 0 && strcmp(d, "()J") == 0) { vm_push(vm, V_LONG(unbox_long(o))); return 1; }
    if (strcmp(n, "floatValue") == 0 && strcmp(d, "()F") == 0) {
        if (o->cls[0] == 'F') vm_push(vm, V_F32(mj_f_from_bits((uint32_t)o->val)));
        else if (o->cls[0] == 'D') vm_push(vm, V_F32((float)mj_d_from_bits((uint64_t)o->val)));
        else vm_push(vm, V_F32((float)o->val));
        return 1;
    }
    if (strcmp(n, "doubleValue") == 0 && strcmp(d, "()D") == 0) { vm_push(vm, V_F64(unbox_double(o))); return 1; }
    if (strcmp(n, "charValue") == 0 && strcmp(d, "()C") == 0) { vm_push(vm, V_INT(unbox_char(o))); return 1; }
    if (strcmp(n, "booleanValue") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(unbox_boolean(o))); return 1; }
    if (strcmp(n, "byteValue") == 0 && strcmp(d, "()B") == 0) { vm_push(vm, V_INT((int64_t)(int8_t)unbox_int(o))); return 1; }
    if (strcmp(n, "shortValue") == 0 && strcmp(d, "()S") == 0) { vm_push(vm, V_INT((int64_t)(int16_t)unbox_int(o))); return 1; }
    if (strcmp(n, "toString") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        vm_push(vm, V_REF(to_string_ref(vm, V_REF(vm_peek(vm, 1).v))));
        return 1;
    }
    if (strcmp(n, "equals") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 1).v);
        bool eq = a && a->kind == OBJ_BOX && a->cls[0] == o->cls[0] && a->val == o->val;
        vm_push(vm, V_INT(eq ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "hashCode") == 0 && strcmp(d, "()I") == 0) {
        vm_push(vm, V_INT((int64_t)i32(o->val)));
        return 1;
    }
    if (strcmp(n, "compareTo") == 0 && (d[0] == '(')) {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 1).v);
        int64_t x = unbox_long(o), y = unbox_long(a);
        vm_push(vm, V_INT(x > y ? 1 : (x < y ? -1 : 0)));
        return 1;
    }
    return 0;
}

// ---------------- System ----------------
static int do_system(VM* vm, const char* n, const char* d) {
    if (strcmp(n, "arraycopy") == 0 && strcmp(d, "(Ljava/lang/Object;ILjava/lang/Object;II)V") == 0) {
        int64_t sr = vm_peek(vm, 5).v, dr = vm_peek(vm, 3).v;
        int sp_ = i32(vm_peek(vm, 4).v), dp = i32(vm_peek(vm, 2).v), cnt = i32(vm_peek(vm, 1).v);
        Obj* s = vm_obj_at(vm, sr);
        Obj* d_ = vm_obj_at(vm, dr);
        if (!s || !d_) { vm_throw(vm, "java/lang/NullPointerException", "arraycopy"); return 1; }
        if (sp_ < 0 || dp < 0 || cnt < 0 || sp_ + cnt > s->len || dp + cnt > d_->len) {
            vm_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "arraycopy");
            return 1;
        }
        if (s->kind == OBJ_LONGARR || s->kind == OBJ_INTARR) {
            if (d_->kind == OBJ_LONGARR && s->kind == OBJ_LONGARR)
                memmove(d_->larr + dp, s->larr + sp_, (size_t)cnt * sizeof(int64_t));
            else if (d_->kind == OBJ_INTARR)
                memmove(d_->arr + dp, s->arr + sp_, (size_t)cnt * sizeof(int32_t));
        } else {
            memmove(d_->arr + dp, s->arr + sp_, (size_t)cnt * sizeof(int32_t));
        }
        return 1;
    }
    if (strcmp(n, "currentTimeMillis") == 0 && strcmp(d, "()J") == 0) {
        vm_push(vm, V_LONG(platform_tick_ms()));
        return 1;
    }
    if (strcmp(n, "nanoTime") == 0 && strcmp(d, "()J") == 0) {
        vm_push(vm, V_LONG(platform_tick_ms() * 1000000LL));
        return 1;
    }
    if (strcmp(n, "exit") == 0 && strcmp(d, "(I)V") == 0) {
        vm->done = true;
        vm->exit_code = i32(vm_peek(vm, 1).v);
        return 1;
    }
    if (strcmp(n, "getProperty") == 0 && strcmp(d, "(Ljava/lang/String;)Ljava/lang/String;") == 0) {
        Obj* s = arg_str(vm, vm_peek(vm, 1).v);
        const char* k = s ? s->str : "";
        const char* r = "null";
        if (strcmp(k, "os.name") == 0) r = "nefuOS";
        else if (strcmp(k, "os.arch") == 0) r = "x86_64";
        else if (strcmp(k, "line.separator") == 0) r = "\n";
        else if (strcmp(k, "file.separator") == 0) r = "/";
        else if (strcmp(k, "java.version") == 0) r = "1.8.0_minijvm";
        if (strcmp(r, "null") == 0) vm_push(vm, V_NULL());
        else vm_push(vm, V_REF(mk_stringz(vm, r)));
        return 1;
    }
    return 0;
}

// ---------------- Arrays ----------------
static int do_arrays(VM* vm, const char* n, const char* d) {
    if (strcmp(n, "sort") == 0 && d[0] == '(' && d[1] == '[') {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 1).v);
        if (!a) { vm_throw(vm, "java/lang/NullPointerException", "null array"); return 1; }
        char et = d[1];
        if (et == 'J' || et == 'D') {
            int64_t* v = a->larr;
            for (int i = 1; i < a->len; i++)
                for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int64_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
        } else if (et == 'L') {
            int32_t* v = a->arr;
            for (int i = 1; i < a->len; i++)
                for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
        } else {
            int32_t* v = a->arr;
            for (int i = 1; i < a->len; i++)
                for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
        }
        return 1;
    }
    if (strcmp(n, "sort") == 0 && strcmp(d, "([Ljava/lang/Object;Ljava/util/Comparator;)V") == 0) return 1;
    if (strcmp(n, "fill") == 0 && d[0] == '(' && d[1] == '[') {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 2).v);
        if (!a) { vm_throw(vm, "java/lang/NullPointerException", "null array"); return 1; }
        int32_t v = i32(vm_peek(vm, 1).v);
        if (a->kind == OBJ_LONGARR) { for (int i = 0; i < a->len; i++) a->larr[i] = v; }
        else { for (int i = 0; i < a->len; i++) a->arr[i] = v; }
        return 1;
    }
    if (strcmp(n, "copyOf") == 0 && d[0] == '(') {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 2).v);
        int nl = i32(vm_peek(vm, 1).v);
        if (!a) { vm_throw(vm, "java/lang/NullPointerException", "null array"); return 1; }
        if (nl < 0) { vm_throw(vm, "java/lang/NegativeArraySizeException", "copyOf"); return 1; }
        Obj* o = vm_new_obj(vm, a->kind);
        if (!o) return 1;
        o->len = nl; o->cap = nl;
        if (a->kind == OBJ_LONGARR) {
            o->larr = (int64_t*)kalloc((size_t)nl * sizeof(int64_t) + 8);
            if (o->larr) {
                for (int i = 0; i < nl; i++) o->larr[i] = (i < a->len) ? a->larr[i] : 0;
            }
        } else {
            o->arr = (int32_t*)kalloc((size_t)nl * sizeof(int32_t) + 4);
            if (o->arr) {
                for (int i = 0; i < nl; i++) o->arr[i] = (i < a->len) ? a->arr[i] : -1;
            }
        }
        vm_push(vm, V_REF((int64_t)(o - vm->objs)));
        return 1;
    }
    if (strcmp(n, "equals") == 0 && d[0] == '(' && d[1] == '[') {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 2).v);
        Obj* b = vm_obj_at(vm, vm_peek(vm, 1).v);
        bool eq = a && b && a->len == b->len;
        if (eq && a->kind == OBJ_LONGARR) { for (int i = 0; i < a->len; i++) if (a->larr[i] != b->larr[i]) { eq = false; break; } }
        else if (eq) { for (int i = 0; i < a->len; i++) if (a->arr[i] != b->arr[i]) { eq = false; break; } }
        vm_push(vm, V_INT(eq ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "toString") == 0 && strcmp(d, "([I)Ljava/lang/String;") == 0) {
        Obj* a = vm_obj_at(vm, vm_peek(vm, 1).v);
        if (!a) vm_push(vm, V_REF(mk_stringz(vm, "null")));
        else {
            Obj* sb = vm_new_obj(vm, OBJ_STRBUF);
            if (!sb) return 1;
            sb->len = 0; sb->cap = 64;
            sb->str = (char*)kalloc(65);
            if (sb->str) sb->str[0] = 0;
            sb_append_s(sb, "[", 1);
            for (int i = 0; i < a->len; i++) {
                if (i) sb_append_s(sb, ", ", 2);
                char b[24]; ksprintf(b, sizeof(b), "%d", i32(a->arr[i]));
                sb_append_s(sb, b, (int)strlen(b));
            }
            sb_append_s(sb, "]", 1);
            vm_push(vm, V_REF(mk_string(vm, sb->str, sb->len)));
        }
        return 1;
    }
    return 0;
}

// ---------------- ArrayList ----------------
// Equality semantics: ref-equal, or content-equal when both are strings
// (ldc materializes a fresh string per occurrence, so ref-only breaks
// contains/indexOf/map keys the way javac code is written).
static bool refs_eq(VM* vm, int64_t a, int64_t b) {
    if (a == b) return true;
    Obj* oa = (a >= 0) ? vm_obj_at(vm, a) : 0;
    Obj* ob = (b >= 0) ? vm_obj_at(vm, b) : 0;
    if (oa && ob && oa->kind == OBJ_STRING && ob->kind == OBJ_STRING)
        return oa->len == ob->len && memcmp(oa->str, ob->str, (size_t)oa->len) == 0;
    return false;
}
static int64_t coll_find(VM* vm, Obj* c, int64_t ref) {
    for (int i = 0; i < c->len; i++)
        if (refs_eq(vm, c->arr[i], ref)) return i;
    return -1;
}
static void coll_grow(Obj* c) {
    if (c->len + 1 > c->cap) {
        int nc = c->cap < 4 ? 8 : c->cap * 2;
        int32_t* ns = (int32_t*)kalloc((size_t)nc * sizeof(int32_t) + 4);
        if (!ns) return;
        if (c->arr) { memcpy(ns, c->arr, (size_t)c->len * sizeof(int32_t)); kfree(c->arr); }
        c->arr = ns;
        c->cap = nc;
    }
}
static int do_arraylist(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) return 1;
    if (!has_this) return 0;
    Obj* c = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!c || c->kind != OBJ_ARR) { vm_throw(vm, "java/lang/NullPointerException", "null list"); return 1; }
    if (strcmp(n, "size") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(c->len)); return 1; }
    if (strcmp(n, "isEmpty") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(c->len == 0 ? 1 : 0)); return 1; }
    if (strcmp(n, "get") == 0 && strcmp(d, "(I)Ljava/lang/Object;") == 0) {
        int i = i32(vm_peek(vm, 1).v);
        if (i < 0 || i >= c->len) { vm_throw(vm, "java/lang/IndexOutOfBoundsException", "get"); return 1; }
        int32_t r = c->arr[i];
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "add") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        coll_grow(c);
        if (c->arr) c->arr[c->len++] = i32(r);
        vm_push(vm, V_INT(1));
        return 1;
    }
    if (strcmp(n, "add") == 0 && strcmp(d, "(ILjava/lang/Object;)V") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        int i = i32(vm_peek(vm, 2).v);
        if (i < 0 || i > c->len) { vm_throw(vm, "java/lang/IndexOutOfBoundsException", "add"); return 1; }
        coll_grow(c);
        if (c->arr) {
            for (int k = c->len; k > i; k--) c->arr[k] = c->arr[k - 1];
            c->arr[i] = i32(r);
            c->len++;
        }
        return 1;
    }
    if (strcmp(n, "set") == 0 && strcmp(d, "(ILjava/lang/Object;)Ljava/lang/Object;") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        int i = i32(vm_peek(vm, 2).v);
        if (i < 0 || i >= c->len) { vm_throw(vm, "java/lang/IndexOutOfBoundsException", "set"); return 1; }
        int32_t old = c->arr[i];
        c->arr[i] = i32(r);
        vm_push(vm, old >= 0 ? V_REF(old) : V_NULL());
        return 1;
    }
    if (strcmp(n, "remove") == 0 && strcmp(d, "(I)Ljava/lang/Object;") == 0) {
        int i = i32(vm_peek(vm, 1).v);
        if (i < 0 || i >= c->len) { vm_throw(vm, "java/lang/IndexOutOfBoundsException", "remove"); return 1; }
        int32_t old = c->arr[i];
        for (int k = i; k < c->len - 1; k++) c->arr[k] = c->arr[k + 1];
        c->len--;
        vm_push(vm, old >= 0 ? V_REF(old) : V_NULL());
        return 1;
    }
    if (strcmp(n, "remove") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        int64_t i = coll_find(vm, c, r);
        if (i < 0) { vm_push(vm, V_INT(0)); return 1; }
        for (int k = (int)i; k < c->len - 1; k++) c->arr[k] = c->arr[k + 1];
        c->len--;
        vm_push(vm, V_INT(1));
        return 1;
    }
    if (strcmp(n, "clear") == 0 && strcmp(d, "()V") == 0) { c->len = 0; return 1; }
    if (strcmp(n, "contains") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        vm_push(vm, V_INT(coll_find(vm, c, r) >= 0 ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "indexOf") == 0 && strcmp(d, "(Ljava/lang/Object;)I") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        vm_push(vm, V_INT(coll_find(vm, c, r)));
        return 1;
    }
    if (strcmp(n, "iterator") == 0 && strcmp(d, "()Ljava/util/Iterator;") == 0) {
        Obj* it = vm_new_obj(vm, OBJ_ITR);
        if (!it) return 1;
        it->fld_count = 1;
        it->flds = (int64_t*)kalloc(sizeof(int64_t));
        if (it->flds) it->flds[0] = vm_peek(vm, 1).v;
        it->val = 0;
        vm_push(vm, V_REF((int64_t)(it - vm->objs)));
        return 1;
    }
    return 0;
}

// ---------------- HashMap / HashSet ----------------
static int64_t map_find(VM* vm, Obj* m, int64_t kref) {
    for (int i = 0; i < m->len; i++)
        if (refs_eq(vm, m->arr[2 * i], kref)) return i;
    return -1;
}
static int do_hashmap(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) return 1;
    if (!has_this) return 0;
    Obj* m = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!m || m->kind != OBJ_MAP) { vm_throw(vm, "java/lang/NullPointerException", "null map"); return 1; }
    if (strcmp(n, "size") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(m->len)); return 1; }
    if (strcmp(n, "isEmpty") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(m->len == 0 ? 1 : 0)); return 1; }
    if (strcmp(n, "put") == 0 && strcmp(d, "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;") == 0) {
        int64_t k = vm_peek(vm, 2).v, v = vm_peek(vm, 1).v;
        int64_t i = map_find(vm, m, k);
        int32_t old = -1;
        if (i >= 0) { old = m->arr[2 * i + 1]; m->arr[2 * i + 1] = i32(v); }
        else {
            if (m->len + 1 > m->cap) {
                int nc = m->cap < 16 ? 32 : m->cap * 2;
                int32_t* ns = (int32_t*)kalloc((size_t)nc * 2 * sizeof(int32_t) + 4);
                if (!ns) { vm_push(vm, V_NULL()); return 1; }
                if (m->arr) { memcpy(ns, m->arr, (size_t)m->len * 2 * sizeof(int32_t)); kfree(m->arr); }
                m->arr = ns; m->cap = nc;
            }
            if (m->arr) { m->arr[2 * m->len] = i32(k); m->arr[2 * m->len + 1] = i32(v); m->len++; }
        }
        vm_push(vm, old >= 0 ? V_REF(old) : V_NULL());
        return 1;
    }
    if (strcmp(n, "get") == 0 && strcmp(d, "(Ljava/lang/Object;)Ljava/lang/Object;") == 0) {
        int64_t k = vm_peek(vm, 1).v;
        int64_t i = map_find(vm, m, k);
        if (i < 0) { vm_push(vm, V_NULL()); return 1; }
        int32_t r = m->arr[2 * (int)i + 1];
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "containsKey") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t k = vm_peek(vm, 1).v;
        vm_push(vm, V_INT(map_find(vm, m, k) >= 0 ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "remove") == 0 && strcmp(d, "(Ljava/lang/Object;)Ljava/lang/Object;") == 0) {
        int64_t k = vm_peek(vm, 1).v;
        int64_t i = map_find(vm, m, k);
        if (i < 0) { vm_push(vm, V_NULL()); return 1; }
        int32_t old = m->arr[2 * (int)i + 1];
        for (int64_t j = i; j < m->len - 1; j++) { m->arr[2 * (int)j] = m->arr[2 * (int)j + 2]; m->arr[2 * (int)j + 1] = m->arr[2 * (int)j + 3]; }
        m->len--;
        vm_push(vm, old >= 0 ? V_REF(old) : V_NULL());
        return 1;
    }
    if (strcmp(n, "clear") == 0 && strcmp(d, "()V") == 0) { m->len = 0; return 1; }
    if (strcmp(n, "keySet") == 0 && strcmp(d, "()Ljava/util/Set;") == 0) {
        Obj* s = vm_new_obj(vm, OBJ_SET);
        if (!s) return 1;
        s->len = m->len; s->cap = m->cap;
        s->arr = (int32_t*)kalloc((size_t)(m->len ? m->len : 1) * sizeof(int32_t) + 4);
        if (s->arr) { for (int i = 0; i < m->len; i++) s->arr[i] = m->arr[2 * i]; }
        vm_push(vm, V_REF((int64_t)(s - vm->objs)));
        return 1;
    }
    if (strcmp(n, "entrySet") == 0 && strcmp(d, "()Ljava/util/Set;") == 0) {
        Obj* s = vm_new_obj(vm, OBJ_SET);
        if (!s) return 1;
        s->len = m->len; s->cap = m->cap;
        s->arr = (int32_t*)kalloc((size_t)(m->len ? m->len : 1) * sizeof(int32_t) + 4);
        if (s->arr) { for (int i = 0; i < m->len; i++) s->arr[i] = m->arr[2 * i]; }
        vm_push(vm, V_REF((int64_t)(s - vm->objs)));
        return 1;
    }
    return 0;
}
static int do_hashset(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) return 1;
    if (!has_this) return 0;
    Obj* s = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!s || s->kind != OBJ_SET) { vm_throw(vm, "java/lang/NullPointerException", "null set"); return 1; }
    if (strcmp(n, "size") == 0 && strcmp(d, "()I") == 0) { vm_push(vm, V_INT(s->len)); return 1; }
    if (strcmp(n, "add") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        bool has = coll_find(vm, s, r) >= 0;
        if (!has) {
            coll_grow(s);
            if (s->arr) s->arr[s->len++] = i32(r);
        }
        vm_push(vm, V_INT(has ? 0 : 1));
        return 1;
    }
    if (strcmp(n, "contains") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        vm_push(vm, V_INT(coll_find(vm, s, r) >= 0 ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "remove") == 0 && strcmp(d, "(Ljava/lang/Object;)Z") == 0) {
        int64_t r = vm_peek(vm, 1).v;
        int64_t i = coll_find(vm, s, r);
        if (i < 0) { vm_push(vm, V_INT(0)); return 1; }
        for (int64_t k = i; k < s->len - 1; k++) s->arr[(int)k] = s->arr[(int)k + 1];
        s->len--;
        vm_push(vm, V_INT(1));
        return 1;
    }
    if (strcmp(n, "iterator") == 0 && strcmp(d, "()Ljava/util/Iterator;") == 0) {
        Obj* it = vm_new_obj(vm, OBJ_ITR);
        if (!it) return 1;
        it->fld_count = 1;
        it->flds = (int64_t*)kalloc(sizeof(int64_t));
        if (it->flds) it->flds[0] = vm_peek(vm, 1).v;
        it->val = 0;
        vm_push(vm, V_REF((int64_t)(it - vm->objs)));
        return 1;
    }
    return 0;
}
static int do_iterator(VM* vm, const char* n, const char* d, bool has_this) {
    if (!has_this) return 0;
    Obj* it = vm_obj_at(vm, vm_peek(vm, 1).v);
    if (!it || it->kind != OBJ_ITR) return 0;
    Obj* host = vm_obj_at(vm, it->flds ? it->flds[0] : -1);
    if (strcmp(n, "hasNext") == 0 && strcmp(d, "()Z") == 0) {
        vm_push(vm, V_INT(host && it->val < host->len ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "next") == 0 && strcmp(d, "()Ljava/lang/Object;") == 0) {
        if (!host || it->val >= host->len) { vm_throw(vm, "java/util/NoSuchElementException", "iterator"); return 1; }
        int32_t r = host->arr[(int)it->val++];
        vm_push(vm, r >= 0 ? V_REF(r) : V_NULL());
        return 1;
    }
    if (strcmp(n, "remove") == 0) return 1;
    return 0;
}
static int do_collections(VM* vm, const char* n, const char* d) {
    if (strcmp(n, "sort") == 0 && strcmp(d, "(Ljava/util/List;)V") == 0) {
        Obj* l = vm_obj_at(vm, vm_peek(vm, 1).v);
        if (l && l->kind == OBJ_ARR) {
            for (int i = 1; i < l->len; i++)
                for (int j = i; j > 0 && l->arr[j - 1] > l->arr[j]; j--) {
                    int32_t t = l->arr[j]; l->arr[j] = l->arr[j - 1]; l->arr[j - 1] = t;
                }
        }
        return 1;
    }
    return 0;
}

// ---------------- java/io ----------------
static int vfs_stat_file(VM* vm, const char* path, uint32_t* size, bool* is_dir) {
    if (!vm->vfs || !vm->vfs->stat) return 1;
    MiniJvmFile mf;
    int r = vm->vfs->stat(vm->vfs->ud, path, &mf);
    if (r == 0) { if (size) *size = mf.size; if (is_dir) *is_dir = mf.is_dir; return 0; }
    return 1;
}
static int64_t path_of(VM* vm, int64_t ref) {
    Obj* o = vm_obj_at(vm, ref);
    if (o && o->kind == OBJ_STRING) return ref;
    if (o && o->kind == OBJ_FILE && o->cls) return mk_stringz(vm, o->cls);
    return -1;
}
static int do_file(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) {
        Obj* f = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
        if (f && f->kind == OBJ_FILE) {
            int64_t p = path_of(vm, vm_peek(vm, 1).v);
            Obj* po = (p >= 0) ? vm_obj_at(vm, p) : 0;
            if (po && po->kind == OBJ_STRING && f->cls == 0) {
                f->cls = (char*)kalloc((size_t)po->len + 1);
                if (f->cls) { memcpy(f->cls, po->str, (size_t)po->len); f->cls[po->len] = 0; }
            }
        }
        return 1;
    }
    if (!has_this) {
        if (strcmp(n, "separator") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
            vm_push(vm, V_REF(mk_stringz(vm, "/")));
            return 1;
        }
        return 0;
    }
    Obj* f = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!f || f->kind != OBJ_FILE) { vm_throw(vm, "java/lang/NullPointerException", "null file"); return 1; }
    const char* path = f->cls ? f->cls : "";
    uint32_t sz = 0; bool isd = false;
    bool exists = vfs_stat_file(vm, path, &sz, &isd) == 0;
    if (strcmp(n, "exists") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(exists ? 1 : 0)); return 1; }
    if (strcmp(n, "isFile") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(exists && !isd ? 1 : 0)); return 1; }
    if (strcmp(n, "isDirectory") == 0 && strcmp(d, "()Z") == 0) { vm_push(vm, V_INT(exists && isd ? 1 : 0)); return 1; }
    if (strcmp(n, "length") == 0 && strcmp(d, "()J") == 0) { vm_push(vm, V_LONG(exists ? sz : 0)); return 1; }
    if (strcmp(n, "getPath") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) { vm_push(vm, V_REF(mk_stringz(vm, path))); return 1; }
    if (strcmp(n, "getName") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        const char* sl = 0;
        for (const char* p = path; *p; p++) if (*p == '/') sl = p;
        vm_push(vm, V_REF(mk_stringz(vm, sl ? sl + 1 : path)));
        return 1;
    }
    if (strcmp(n, "getAbsolutePath") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        vm_push(vm, V_REF(mk_stringz(vm, path)));
        return 1;
    }
    if (strcmp(n, "delete") == 0 && strcmp(d, "()Z") == 0) {
        bool ok = vm->vfs && vm->vfs->remove && vm->vfs->remove(vm->vfs->ud, path) == 0;
        vm_push(vm, V_INT(ok ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "mkdir") == 0 && strcmp(d, "()Z") == 0) {
        bool ok = vm->vfs && vm->vfs->mkdir && vm->vfs->mkdir(vm->vfs->ud, path) == 0;
        vm_push(vm, V_INT(ok ? 1 : 0));
        return 1;
    }
    if (strcmp(n, "list") == 0 && strcmp(d, "()[Ljava/lang/String;") == 0) {
        if (!exists || !isd || !vm->vfs || !vm->vfs->list) { vm_push(vm, V_NULL()); return 1; }
        Obj* names = vm_new_obj(vm, OBJ_STRARR);
        if (!names) return 1;
        char buf[1024];
        int r = vm->vfs->list(vm->vfs->ud, path, buf, sizeof(buf));
        if (r < 0) { vm_push(vm, V_NULL()); return 1; }
        names->cap = 16; names->arr = (int32_t*)kalloc(16 * sizeof(int32_t) + 4);
        if (!names->arr) { vm_push(vm, V_NULL()); return 1; }
        int cnt = 0;
        const char* q = buf;
        while (*q && cnt < 64) {
            const char* e = q;
            while (*e && *e != '\n') e++;
            int64_t s = mk_string(vm, q, (int)(e - q));
            if (s >= 0) {
                if (cnt >= names->cap) {
                    int nc = names->cap * 2;
                    int32_t* ns = (int32_t*)kalloc((size_t)nc * sizeof(int32_t) + 4);
                    if (!ns) break;
                    if (names->arr) { memcpy(ns, names->arr, (size_t)cnt * sizeof(int32_t)); kfree(names->arr); }
                    names->arr = ns; names->cap = nc;
                }
                names->arr[cnt++] = i32(s);
            }
            q = *e ? e + 1 : e;
        }
        names->len = cnt;
        vm_push(vm, V_REF((int64_t)(names - vm->objs)));
        return 1;
    }
    return 0;
}
static int do_stream(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) {
        Obj* st = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
        if (st && st->kind == OBJ_STREAM && st->flds) {
            int64_t p = path_of(vm, vm_peek(vm, 1).v);
            Obj* po = (p >= 0) ? vm_obj_at(vm, p) : 0;
            if (po && po->kind == OBJ_STRING) {
                st->flds[0] = p;
            } else if (strcmp(st->cls ? st->cls : "", "BAIS") == 0 || strcmp(st->cls ? st->cls : "", "BAOS") == 0) {
                st->flds[0] = vm_peek(vm, 1).v;   // byte array ref
            }
            st->flds[1] = 0;
        }
        return 1;
    }
    if (!has_this) {
        return 0;
    }
    Obj* st = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (!st || st->kind != OBJ_STREAM) { vm_throw(vm, "java/lang/NullPointerException", "null stream"); return 1; }
    const char* tag = st->cls ? st->cls : "";
    if (strcmp(n, "read") == 0 && strcmp(d, "()I") == 0) {
        int r = -1;
        if (strcmp(tag, "BAIS") == 0) {
            Obj* ba = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
            if (ba && st->flds[1] < ba->len) r = ba->arr[(int)st->flds[1]++];
        } else if (strcmp(tag, "FIS") == 0) {
            // no seek support: read single byte via a 1-byte buffer at cursor
            if (vm->vfs && vm->vfs->read) {
                Obj* po = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
                const char* path = (po && po->kind == OBJ_STRING) ? po->str : "";
                uint8_t b = 0;
                int rr = vm->vfs->read(vm->vfs->ud, path, &b, (uint32_t)st->flds[1], 1);
                if (rr == 1) { r = b; st->flds[1]++; }
            }
        }
        vm_push(vm, V_INT(r));
        return 1;
    }
    if (strcmp(n, "read") == 0 && strcmp(d, "([BII)I") == 0) {
        Obj* ba = vm_obj_at(vm, vm_peek(vm, 3).v);
        int off = i32(vm_peek(vm, 2).v), len = i32(vm_peek(vm, 1).v);
        int got = -1;
        if (strcmp(tag, "BAIS") == 0) {
            Obj* src = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
            if (src && ba) {
                got = 0;
                while (got < len && st->flds[1] < src->len)
                    ba->arr[off + got++] = src->arr[(int)st->flds[1]++];
            }
        } else if (strcmp(tag, "FIS") == 0) {
            if (vm->vfs && vm->vfs->read && ba) {
                Obj* po = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
                const char* path = (po && po->kind == OBJ_STRING) ? po->str : "";
                got = vm->vfs->read(vm->vfs->ud, path, (uint8_t*)(ba->arr + off), (uint32_t)st->flds[1], (uint32_t)len);
                if (got > 0) st->flds[1] += got;
            }
        }
        vm_push(vm, V_INT(got));
        return 1;
    }
    if (strcmp(n, "write") == 0 && strcmp(d, "(I)V") == 0) {
        int b = i32(vm_peek(vm, 1).v);
        if (strcmp(tag, "BAOS") == 0) {
            Obj* ba = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
            if (!ba) {
                ba = vm_new_obj(vm, OBJ_INTARR);
                if (ba) { ba->len = 0; ba->cap = 16; ba->arr = (int32_t*)kalloc(16 * sizeof(int32_t) + 4); if (ba->arr) memset(ba->arr, 0, 16 * sizeof(int32_t)); st->flds[0] = (int64_t)(ba - vm->objs); }
            }
            if (ba && ba->len + 1 > ba->cap) {
                int nc = ba->cap * 2;
                int32_t* ns = (int32_t*)kalloc((size_t)nc * sizeof(int32_t) + 4);
                if (ns) { if (ba->arr) { memcpy(ns, ba->arr, (size_t)ba->len * sizeof(int32_t)); kfree(ba->arr); } ba->arr = ns; ba->cap = nc; }
            }
            if (ba && ba->arr) ba->arr[ba->len++] = b;
        } else if (strcmp(tag, "FOS") == 0) {
            if (vm->vfs && vm->vfs->write) {
                Obj* po = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
                const char* path = (po && po->kind == OBJ_STRING) ? po->str : "";
                uint8_t bb = (uint8_t)b;
                vm->vfs->write(vm->vfs->ud, path, &bb, (uint32_t)st->flds[1], 1);
                st->flds[1]++;
            }
        }
        return 1;
    }
    if (strcmp(n, "write") == 0 && strcmp(d, "([BII)V") == 0) {
        Obj* src = vm_obj_at(vm, vm_peek(vm, 3).v);
        int off = i32(vm_peek(vm, 2).v), len = i32(vm_peek(vm, 1).v);
        if (strcmp(tag, "BAOS") == 0) {
            Obj* ba = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
            if (!ba) {
                ba = vm_new_obj(vm, OBJ_INTARR);
                if (ba) { ba->len = 0; ba->cap = 16; ba->arr = (int32_t*)kalloc(16 * sizeof(int32_t) + 4); if (ba->arr) memset(ba->arr, 0, 16 * sizeof(int32_t)); st->flds[0] = (int64_t)(ba - vm->objs); }
            }
            if (ba) {
                if (ba->len + len > ba->cap) {
                    int nc = ba->cap;
                    while (nc < ba->len + len) nc *= 2;
                    int32_t* ns = (int32_t*)kalloc((size_t)nc * sizeof(int32_t) + 4);
                    if (ns) { if (ba->arr) { memcpy(ns, ba->arr, (size_t)ba->len * sizeof(int32_t)); kfree(ba->arr); } ba->arr = ns; ba->cap = nc; }
                }
                if (ba->arr && src) { for (int i = 0; i < len; i++) ba->arr[ba->len++] = src->arr[off + i]; }
            }
        } else if (strcmp(tag, "FOS") == 0) {
            if (vm->vfs && vm->vfs->write && src) {
                Obj* po = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
                const char* path = (po && po->kind == OBJ_STRING) ? po->str : "";
                vm->vfs->write(vm->vfs->ud, path, (uint8_t*)(src->arr + off), (uint32_t)st->flds[1], (uint32_t)len);
                st->flds[1] += len;
            }
        }
        return 1;
    }
    if (strcmp(n, "toByteArray") == 0 && strcmp(d, "()[B") == 0) {
        Obj* ba = vm_obj_at(vm, st->flds ? st->flds[0] : -1);
        Obj* o = vm_new_obj(vm, OBJ_INTARR);
        if (o) {
            o->len = ba ? ba->len : 0; o->cap = o->len;
            o->arr = (int32_t*)kalloc((size_t)(o->len ? o->len : 1) * sizeof(int32_t) + 4);
            if (o->arr) { for (int i = 0; i < o->len; i++) o->arr[i] = ba->arr[i]; }
            vm_push(vm, V_REF((int64_t)(o - vm->objs)));
        }
        return 1;
    }
    if (strcmp(n, "close") == 0 && strcmp(d, "()V") == 0) return 1;
    if (strcmp(n, "flush") == 0 && strcmp(d, "()V") == 0) return 1;
    return 0;
}

// ---------------- PrintStream ----------------
static int do_printstream(VM* vm, const char* n, const char* d) {
    bool is_println = strcmp(n, "println") == 0;
    bool is_print = strcmp(n, "print") == 0;
    if (!is_print && !is_println) return 0;
    int nargs = desc_arg_slots(d);
    Val v = vm_peek(vm, 1);
    if (nargs == 0) {
        vm_emit(vm, "\n");
        return 1;
    }
    char argt = d[1];
    if (argt == 'C') {
        char c = (char)i32(v.v);
        vm_emit_char(vm, c);
        if (is_println) vm_emit(vm, "\n");
        return 1;
    }
    if (argt == 'Z') {
        vm_emit(vm, i32(v.v) ? "true" : "false");
        if (is_println) vm_emit(vm, "\n");
        return 1;
    }
    int64_t sref = to_string_ref(vm, v);
    Obj* so = (sref >= 0) ? vm_obj_at(vm, sref) : 0;
    if (so && so->kind == OBJ_STRING) {
        vm_emit(vm, so->str);
        if (is_println) vm_emit(vm, "\n");
    }
    return 1;
}

// ---------------- Throwable family ----------------
static int do_throwable(VM* vm, const char* n, const char* d, bool has_this) {
    if (strcmp(n, "<init>") == 0) {
        // invokespecial carries this; seed message/cause from the ctor args
        int slots = desc_arg_slots(d);
        Obj* e = vm_obj_at(vm, vm_peek(vm, slots + 1).v);
        if (e && e->kind == OBJ_INST && e->flds) {
            if (strcmp(d, "(Ljava/lang/String;)V") == 0)
                e->flds[0] = vm_peek(vm, 1).v;
            else if (strcmp(d, "(Ljava/lang/Throwable;)V") == 0)
                e->flds[1] = vm_peek(vm, 1).v;
            else if (strcmp(d, "(Ljava/lang/String;Ljava/lang/Throwable;)V") == 0) {
                e->flds[0] = vm_peek(vm, 2).v;
                e->flds[1] = vm_peek(vm, 1).v;
            }
        }
        return 1;
    }
    if (!has_this) {
        if (strcmp(n, "fillInStackTrace") == 0) return 1;
        return 0;
    }
    Obj* e = vm_obj_at(vm, vm_peek(vm, desc_arg_slots(d) + 1).v);
    if (strcmp(n, "getMessage") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        int64_t m = (e && e->flds) ? e->flds[0] : -1;
        vm_push(vm, m >= 0 ? V_REF(m) : V_NULL());
        return 1;
    }
    if (strcmp(n, "getCause") == 0 && strcmp(d, "()Ljava/lang/Throwable;") == 0) {
        int64_t m = (e && e->flds) ? e->flds[1] : -1;
        vm_push(vm, m >= 0 ? V_REF(m) : V_NULL());
        return 1;
    }
    if (strcmp(n, "toString") == 0 && strcmp(d, "()Ljava/lang/String;") == 0) {
        int64_t m = (e && e->flds) ? e->flds[0] : -1;
        Obj* mo = (m >= 0) ? vm_obj_at(vm, m) : 0;
        const char* cn = e && e->cls ? e->cls : "java.lang.Throwable";
        const char* ms = (mo && mo->kind == OBJ_STRING) ? mo->str : "";
        char b[256];
        if (ms[0]) ksprintf(b, sizeof(b), "%s: %s", cn, ms);
        else ksprintf(b, sizeof(b), "%s", cn);
        vm_push(vm, V_REF(mk_stringz(vm, b)));
        return 1;
    }
    if (strcmp(n, "printStackTrace") == 0) return 1;
    return 0;
}

} // namespace

// ---------------- main dispatch ----------------
static bool desc_returns_void(const char* d) {
    const char* p = d;
    while (*p && *p != ')') p++;
    return *p == ')' && p[1] == 'V';
}
static int dispatch(VM* vm, const char* owner, const char* name, const char* desc, bool has_this) {
    if (strcmp(owner, "java/lang/String") == 0) return do_string(vm, name, desc, has_this);
    if (strcmp(owner, "java/lang/StringBuilder") == 0 || strcmp(owner, "java/lang/StringBuffer") == 0)
        return do_stringbuilder(vm, name, desc, has_this);
    if (strcmp(owner, "java/lang/Math") == 0) return do_math(vm, name, desc);
    if (strcmp(owner, "java/lang/Integer") == 0 || strcmp(owner, "java/lang/Long") == 0 ||
        strcmp(owner, "java/lang/Boolean") == 0 || strcmp(owner, "java/lang/Character") == 0 ||
        strcmp(owner, "java/lang/Byte") == 0 || strcmp(owner, "java/lang/Short") == 0 ||
        strcmp(owner, "java/lang/Float") == 0 || strcmp(owner, "java/lang/Double") == 0)
        return do_box(vm, owner, name, desc, has_this);
    if (strcmp(owner, "java/lang/System") == 0) return do_system(vm, name, desc);
    if (strcmp(owner, "java/util/Arrays") == 0) return do_arrays(vm, name, desc);
    if (strcmp(owner, "java/util/ArrayList") == 0 || strcmp(owner, "java/util/List") == 0)
        return do_arraylist(vm, name, desc, has_this);
    if (strcmp(owner, "java/util/HashMap") == 0 || strcmp(owner, "java/util/Map") == 0)
        return do_hashmap(vm, name, desc, has_this);
    if (strcmp(owner, "java/util/HashSet") == 0 || strcmp(owner, "java/util/Set") == 0)
        return do_hashset(vm, name, desc, has_this);
    if (strcmp(owner, "java/util/Iterator") == 0) return do_iterator(vm, name, desc, has_this);
    if (strcmp(owner, "java/util/Collections") == 0) return do_collections(vm, name, desc);
    if (strcmp(owner, "java/io/File") == 0) return do_file(vm, name, desc, has_this);
    if (strcmp(owner, "java/io/FileInputStream") == 0 || strcmp(owner, "java/io/FileOutputStream") == 0 ||
        strcmp(owner, "java/io/ByteArrayInputStream") == 0 || strcmp(owner, "java/io/ByteArrayOutputStream") == 0 ||
        strcmp(owner, "java/io/InputStream") == 0 || strcmp(owner, "java/io/OutputStream") == 0)
        return do_stream(vm, name, desc, has_this);
    if (strcmp(owner, "java/io/PrintStream") == 0) return do_printstream(vm, name, desc);
    if (is_throwable_like(owner)) return do_throwable(vm, name, desc, has_this);
    if (strcmp(owner, "java/lang/Object") == 0) {
        if (strcmp(name, "<init>") == 0) return 1;
        if (strcmp(name, "toString") == 0 && strcmp(desc, "()Ljava/lang/String;") == 0) {
            vm_push(vm, V_REF(mk_stringz(vm, "java.lang.Object@0")));
            return 1;
        }
        if (strcmp(name, "equals") == 0 && strcmp(desc, "(Ljava/lang/Object;)Z") == 0) {
            Val a = vm_peek(vm, 2), b = vm_peek(vm, 1);
            bool eq = (a.kind == K_NULL && b.kind == K_NULL) ||
                      (a.kind == K_REF && b.kind == K_REF && a.v == b.v);
            vm_push(vm, V_INT(eq ? 1 : 0));
            return 1;
        }
        if (strcmp(name, "hashCode") == 0 && strcmp(desc, "()I") == 0) {
            vm_push(vm, V_INT((int64_t)i32(vm_peek(vm, 1).v)));
            return 1;
        }
        if (strcmp(name, "getClass") == 0 && strcmp(desc, "()Ljava/lang/Class;") == 0) {
            Obj* o = vm_new_obj(vm, OBJ_CLASS);
            if (o) {
                o->cls = (char*)kalloc(1);
                if (o->cls) o->cls[0] = 0;
                vm_push(vm, V_REF((int64_t)(o - vm->objs)));
            }
            return 1;
        }
        if (strcmp(name, "getClass") == 0 && strcmp(desc, "()Ljava/lang/Class;") == 0) return 1;
        return 0;
    }
    return 0;
}

int invoke_builtin(VM* vm, const char* owner, const char* name, const char* desc, bool has_this) {
    int r = dispatch(vm, owner, name, desc, has_this);
    if (r) {
        // unwind: pop result (if any) then this + args, re-push result
        bool ret = !desc_returns_void(desc);
        Val res;
        if (ret) res = vm_pop(vm);
        int nargs = desc_arg_slots(desc);
        for (int i = 0; i < nargs + (has_this ? 1 : 0); i++) vm_pop(vm);
        if (ret) vm_push(vm, res);
    }
    return r;
}

} // namespace nefu
