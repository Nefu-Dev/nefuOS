// ============================================================================
// nefu::minilang —— 运行时值类型与引用计数（实现）
// ============================================================================
#include "value.h"
#include <string.h>

namespace nefu {
namespace minilang {

static StrObj* val_to_strobj_obj(Value v, char* buf);

void obj_retain(Obj* o) {
    if (o) o->ref++;
}

static void obj_destroy(Obj* o) {
    if (!o) return;
    switch (o->otype) {
    case O_STRING: {
        StrObj* s = (StrObj*)o;
        delete[] s->s;
        delete s;
        break;
    }
    case O_ARRAY: {
        ArrObj* a = (ArrObj*)o;
        for (int i = 0; i < a->len; i++) val_drop(a->items[i]);
        delete[] a->items;
        delete a;
        break;
    }
    case O_CLOSURE: {
        ClosureObj* c = (ClosureObj*)o;
        if (c->prog) obj_release((Obj*)c->prog);
        if (c->env) env_release(c->env);
        delete c;
        break;
    }
    case O_PROGRAM: {
        ProgramObj* p = (ProgramObj*)o;
        program_free(p);
        break;
    }
    case O_BUILTIN: {
        BuiltinObj* b = (BuiltinObj*)o;
        delete b;
        break;
    }
    }
}

void obj_release(Obj* o) {
    if (!o) return;
    o->ref--;
    if (o->ref <= 0) obj_destroy(o);
}

Value val_dup(Value v) {
    if (v.type == V_OBJ && v.obj) obj_retain(v.obj);
    return v;
}

void val_drop(Value v) {
    if (v.type == V_OBJ && v.obj) obj_release(v.obj);
}

Value val_make_null() {
    Value v; v.type = V_NULL; v.i = 0; v.obj = 0; return v;
}
Value val_make_bool(bool b) {
    Value v; v.type = V_BOOL; v.i = b ? 1 : 0; v.obj = 0; return v;
}
Value val_make_int(int64_t n) {
    Value v; v.type = V_INT; v.i = n; v.obj = 0; return v;
}
Value val_make_fx(fx::fix f) {
    Value v; v.type = V_FX; v.i = (int64_t)f; v.obj = 0; return v;
}
Value val_make_fx_int(int64_t n) {
    return val_make_fx(fx::itofix((int)n));
}
Value val_make_str(const char* s) {
    if (!s) s = "";
    int len = (int)strlen(s);
    return val_make_str_n(s, len);
}
Value val_make_str_n(const char* s, int len) {
    StrObj* o = new StrObj();
    o->otype = O_STRING;
    o->ref   = 1;
    o->len   = len;
    o->s     = new char[len + 1];
    if (o->s) {
        for (int i = 0; i < len; i++) o->s[i] = s[i];
        o->s[len] = 0;
    } else {
        o->len = 0;
    }
    Value v; v.type = V_OBJ; v.i = 0; v.obj = o; return v;
}
Value val_make_arr(int reserve) {
    ArrObj* o = new ArrObj();
    o->otype = O_ARRAY;
    o->ref   = 1;
    o->len   = 0;
    o->cap   = reserve > 0 ? reserve : 4;
    o->items = new Value[o->cap > 0 ? o->cap : 1];
    for (int i = 0; i < o->cap; i++) { o->items[i].type = V_NULL; o->items[i].i = 0; o->items[i].obj = 0; }
    Value v; v.type = V_OBJ; v.i = 0; v.obj = o; return v;
}
Value val_make_closure(FuncNode* fn, ProgramObj* prog, Env* env) {
    ClosureObj* o = new ClosureObj();
    o->otype = O_CLOSURE;
    o->ref   = 1;
    o->fn    = fn;
    o->prog  = prog;
    o->env   = env;
    if (prog) obj_retain((Obj*)prog);
    if (env)  env_retain(env);
    Value v; v.type = V_OBJ; v.i = 0; v.obj = o; return v;
}
Value val_make_builtin(const char* name, BuiltinFn fn) {
    BuiltinObj* o = new BuiltinObj();
    o->otype = O_BUILTIN;
    o->ref   = 1;
    o->name  = name;
    o->fn    = fn;
    Value v; v.type = V_OBJ; v.i = 0; v.obj = o; return v;
}
Value val_make_program() {
    ProgramObj* p = new ProgramObj();
    p->otype = O_PROGRAM;
    p->ref   = 1;
    Value v; v.type = V_OBJ; v.i = 0; v.obj = p; return v;
}

// ---------------- 访问器 ----------------
bool val_is_null(Value v) { return v.type == V_NULL; }

bool val_is_truthy(Value v) {
    switch (v.type) {
    case V_NULL:  return false;
    case V_BOOL:  return v.i != 0;
    case V_INT:   return v.i != 0;
    case V_FX:    return v.i != 0;
    case V_OBJ:
        if (!v.obj) return false;
        if (v.obj->otype == O_STRING) return ((StrObj*)v.obj)->len > 0;
        if (v.obj->otype == O_ARRAY)  return ((ArrObj*)v.obj)->len > 0;
        return true;
    }
    return false;
}

int64_t val_as_int(Value v) {
    switch (v.type) {
    case V_INT:  return v.i;
    case V_BOOL: return v.i ? 1 : 0;
    case V_FX:   return fx::fixtoi((fx::fix)v.i);
    case V_NULL: return 0;
    case V_OBJ:
        if (v.obj && v.obj->otype == O_STRING) {
            const char* s = ((StrObj*)v.obj)->s;
            int sign = 1; int n = 0;
            while (*s == ' ') s++;
            if (*s == '-') { sign = -1; s++; }
            else if (*s == '+') s++;
            while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
            return (int64_t)sign * n;
        }
        return 0;
    }
    return 0;
}

fx::fix val_as_fx(Value v) {
    switch (v.type) {
    case V_FX:   return (fx::fix)v.i;
    case V_INT:  return fx::itofix((int)v.i);
    case V_BOOL: return v.i ? fx::FX_ONE : 0;
    case V_NULL: return 0;
    case V_OBJ:
        if (v.obj && v.obj->otype == O_STRING) {
            const char* s = ((StrObj*)v.obj)->s;
            int sign = 1;
            while (*s == ' ') s++;
            if (*s == '-') { sign = -1; s++; }
            else if (*s == '+') s++;
            int32_t whole = 0;
            while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); s++; }
            int32_t frac = 0, scale = 1;
            if (*s == '.') {
                s++;
                while (*s >= '0' && *s <= '9' && scale < 100000) {
                    frac = frac * 10 + (*s - '0');
                    scale *= 10;
                    s++;
                }
            }
            fx::fix r = fx::itofix(whole);
            r += (fx::fix)(((int64_t)frac * fx::FX_ONE) / scale);
            return sign < 0 ? (fx::fix)(-r) : r;
        }
        return 0;
    }
    return 0;
}

StrObj* val_as_str(Value v) {
    if (v.type == V_OBJ && v.obj && v.obj->otype == O_STRING) return (StrObj*)v.obj;
    return 0;
}
ArrObj* val_as_arr(Value v) {
    if (v.type == V_OBJ && v.obj && v.obj->otype == O_ARRAY) return (ArrObj*)v.obj;
    return 0;
}
ClosureObj* val_as_closure(Value v) {
    if (v.type == V_OBJ && v.obj && v.obj->otype == O_CLOSURE) return (ClosureObj*)v.obj;
    return 0;
}
BuiltinObj* val_as_builtin(Value v) {
    if (v.type == V_OBJ && v.obj && v.obj->otype == O_BUILTIN) return (BuiltinObj*)v.obj;
    return 0;
}
ProgramObj* val_as_program(Value v) {
    if (v.type == V_OBJ && v.obj && v.obj->otype == O_PROGRAM) return (ProgramObj*)v.obj;
    return 0;
}

const char* val_type_name(Value v) {
    switch (v.type) {
    case V_NULL:  return "null";
    case V_INT:   return "int";
    case V_FX:    return "float";
    case V_BOOL:  return "bool";
    case V_OBJ:
        if (!v.obj) return "null";
        switch (v.obj->otype) {
        case O_STRING:  return "str";
        case O_ARRAY:   return "array";
        case O_CLOSURE: return "function";
        case O_BUILTIN: return "builtin";
        case O_PROGRAM: return "program";
        }
    }
    return "?";
}

// ---------------- 值转字符串 ----------------
StrObj* val_to_strobj(Value v) {
    char buf[128];
    switch (v.type) {
    case V_NULL:
        return (StrObj*)val_make_str("null").obj;
    case V_BOOL:
        return (StrObj*)val_make_str(v.i ? "true" : "false").obj;
    case V_INT: {
        char tmp[32]; int pos = 0;
        int64_t n = v.i;
        bool neg = n < 0;
        if (neg) n = -n;
        if (n == 0) tmp[pos++] = '0';
        while (n > 0) { tmp[pos++] = (char)('0' + (n % 10)); n /= 10; }
        int w = 0;
        if (neg) buf[w++] = '-';
        while (pos > 0) buf[w++] = tmp[--pos];
        buf[w] = 0;
        return (StrObj*)val_make_str(buf).obj;
    }
    case V_FX: {
        fx::fix f = (fx::fix)v.i;
        int whole = fx::fixtoi(f);
        fx::fix frac = fx::fx_abs(f) & 0xFFFF;
        int thou = (int)(((int64_t)frac * 1000 + 32768) >> 16);
        if (thou >= 1000) thou = 999;
        bool neg = f < 0;
        int w = 0;
        if (neg) buf[w++] = '-';
        char tmp[16]; int pos = 0;
        int wv = neg ? -whole : whole;
        if (wv == 0) tmp[pos++] = '0';
        while (wv > 0) { tmp[pos++] = (char)('0' + (wv % 10)); wv /= 10; }
        while (pos > 0) buf[w++] = tmp[--pos];
        buf[w++] = '.';
        buf[w++] = (char)('0' + (thou / 100) % 10);
        buf[w++] = (char)('0' + (thou / 10) % 10);
        buf[w++] = (char)('0' + (thou % 10));
        buf[w] = 0;
        return (StrObj*)val_make_str(buf).obj;
    }
    case V_OBJ:
        return val_to_strobj_obj(v, buf);
    }
    return (StrObj*)val_make_str("?").obj;
}

// array ops begin
int arr_len(ArrObj* a) { return a ? a->len : 0; }

bool arr_push(ArrObj* a, Value v) {
    if (!a) return false;
    if (a->len >= a->cap) {
        int nc = a->cap ? a->cap * 2 : 4;
        Value* ni = new Value[nc];
        if (!ni) return false;
        for (int i = 0; i < nc; i++) { ni[i].type = V_NULL; ni[i].i = 0; ni[i].obj = 0; }
        for (int i = 0; i < a->len; i++) ni[i] = a->items[i];
        delete[] a->items;
        a->items = ni;
        a->cap = nc;
    }
    a->items[a->len++] = val_dup(v);
    return true;
}

bool arr_set(ArrObj* a, int idx, Value v) {
    if (!a || idx < 0 || idx >= a->len) return false;
    val_drop(a->items[idx]);
    a->items[idx] = val_dup(v);
    return true;
}

Value arr_get(ArrObj* a, int idx) {
    if (!a || idx < 0 || idx >= a->len) return val_make_null();
    return val_dup(a->items[idx]);
}

void program_add_arena(ProgramObj* p, void* ptr) {
    if (p && ptr) p->arena.push(ptr);
}

void program_free(ProgramObj* p) {
    if (!p) return;
    for (int i = 0; i < p->arena.size(); i++) {
        extern void minilang_ast_box_delete(void* box);
        minilang_ast_box_delete(p->arena[i]);
    }
    delete p;
}

// ---------------- 对象值转字符串（静态辅助定义） ----------------
static StrObj* val_to_strobj_obj(Value v, char* buf) {
    (void)buf;
    if (!v.obj) return (StrObj*)val_make_str("null").obj;
    if (v.obj->otype == O_STRING) {
        StrObj* s = (StrObj*)v.obj;
        obj_retain(v.obj);
        return s;
    }
    if (v.obj->otype == O_ARRAY) {
        ArrObj* a = (ArrObj*)v.obj;
        String out = String("[");
        for (int i = 0; i < a->len; i++) {
            StrObj* e = val_to_strobj(a->items[i]);
            if (i > 0) out += ", ";
            out += e->s;
            Value tmp; tmp.type = V_OBJ; tmp.i = 0; tmp.obj = e;
            val_drop(tmp);
        }
        out += "]";
        return (StrObj*)val_make_str(out.c_str()).obj;
    }
    if (v.obj->otype == O_CLOSURE)  return (StrObj*)val_make_str("[function]").obj;
    if (v.obj->otype == O_BUILTIN)  return (StrObj*)val_make_str("[builtin]").obj;
    if (v.obj->otype == O_PROGRAM)  return (StrObj*)val_make_str("[program]").obj;
    return (StrObj*)val_make_str("[obj]").obj;
}

// ---------------- 自测试 ----------------
int value_self_test() {
    int fails = 0;
    {
        Value a = val_make_int(42);
        if (val_as_int(a) != 42) fails++;
        if (!val_is_truthy(a)) fails++;
        val_drop(a);
        Value z = val_make_int(0);
        if (val_is_truthy(z)) fails++;
        val_drop(z);
        Value t = val_make_bool(true);
        if (!val_is_truthy(t)) fails++;
        if (val_as_int(t) != 1) fails++;
        val_drop(t);
        Value n = val_make_null();
        if (val_is_truthy(n)) fails++;
        val_drop(n);
    }
    {
        Value s = val_make_str("hello");
        StrObj* so = val_as_str(s);
        if (!so || so->len != 5) fails++;
        if (so && so->s[0] != 'h') fails++;
        Value s2 = val_dup(s);
        if (so->ref != 2) fails++;
        val_drop(s2);
        if (so->ref != 1) fails++;
        StrObj* t = val_to_strobj(s);
        if (t != so) fails++;
        Value tmp; tmp.type = V_OBJ; tmp.i = 0; tmp.obj = t;
        val_drop(tmp);
        val_drop(s);
    }
    {
        Value arr = val_make_arr();
        ArrObj* a = val_as_arr(arr);
        arr_push(a, val_make_int(1));
        arr_push(a, val_make_int(2));
        arr_push(a, val_make_int(3));
        if (arr_len(a) != 3) fails++;
        Value g0 = arr_get(a, 0);
        if (val_as_int(g0) != 1) fails++;
        val_drop(g0);
        arr_set(a, 1, val_make_int(20));
        Value g1 = arr_get(a, 1);
        if (val_as_int(g1) != 20) fails++;
        val_drop(g1);
        StrObj* rep = val_to_strobj(arr);
        if (!rep) fails++;
        Value tmp; tmp.type = V_OBJ; tmp.i = 0; tmp.obj = rep;
        val_drop(tmp);
        val_drop(arr);
    }
    {
        Value f = val_make_fx(fx::fx_mul(fx::itofix(3), fx::itofix(2)));
        if (val_as_int(f) != 6) fails++;
        Value half = val_make_fx(fx::FX_HALF);
        if (val_as_fx(half) != fx::FX_HALF) fails++;
        val_drop(f); val_drop(half);
    }
    {
        if (strcmp(val_type_name(val_make_int(1)), "int") != 0) fails++;
        if (strcmp(val_type_name(val_make_bool(true)), "bool") != 0) fails++;
        if (strcmp(val_type_name(val_make_null()), "null") != 0) fails++;
        Value s = val_make_str("x");
        if (strcmp(val_type_name(s), "str") != 0) fails++;
        val_drop(s);
    }
    return fails;
}

} // namespace minilang
} // namespace nefu


