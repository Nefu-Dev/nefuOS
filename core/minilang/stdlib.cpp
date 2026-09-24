// ============================================================================
// nefu::minilang —— 标准库（实现）
// ============================================================================
#include "stdlib.h"
#include <string.h>

namespace nefu {
namespace minilang {

// split(s, sep): 把字符串按分隔符切成数组
static Value s_split(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    StrObj* sep = val_as_str(argv[1]);
    Value arr = val_make_arr(4);
    ArrObj* a = val_as_arr(arr);
    if (!s || !sep || sep->len == 0) {
        arr_push(a, val_dup(argv[0]));
        return arr;
    }
    const char* src = s->s;
    int slen = s->len, seplen = sep->len;
    int start = 0;
    for (int i = 0; i <= slen - seplen; i++) {
        bool match = true;
        for (int j = 0; j < seplen; j++) {
            if (src[i + j] != sep->s[j]) { match = false; break; }
        }
        if (match) {
            arr_push(a, val_make_str_n(src + start, i - start));
            i += seplen - 1;
            start = i + 1;
        }
    }
    arr_push(a, val_make_str_n(src + start, slen - start));
    return arr;
}

// join(arr, sep): 把字符串数组用分隔符连接
static Value s_join(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    StrObj* sep = val_as_str(argv[1]);
    String out;
    if (a) {
        for (int i = 0; i < a->len; i++) {
            StrObj* e = val_to_strobj(a->items[i]);
            if (i > 0 && sep) out += sep->s;
            out += e->s;
            Value tmp; tmp.type = V_OBJ; tmp.i = 0; tmp.obj = e; val_drop(tmp);
        }
    }
    return val_make_str(out.c_str());
}

// find(s, sub): 返回子串起始下标（0 基），找不到返回 -1
static Value s_find(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    StrObj* sub = val_as_str(argv[1]);
    if (!s || !sub) return val_make_int(-1);
    if (sub->len == 0) return val_make_int(0);
    for (int i = 0; i <= s->len - sub->len; i++) {
        bool match = true;
        for (int j = 0; j < sub->len; j++) {
            if (s->s[i + j] != sub->s[j]) { match = false; break; }
        }
        if (match) return val_make_int(i);
    }
    return val_make_int(-1);
}

// replace(s, old, new): 把 old 全部替换为 new
static Value s_replace(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    StrObj* o = val_as_str(argv[1]);
    StrObj* n = val_as_str(argv[2]);
    if (!s || !o) return val_make_str(s ? s->s : "");
    String out;
    int i = 0;
    while (i < s->len) {
        bool match = (o->len > 0);
        for (int j = 0; j < o->len && i + j < s->len; j++)
            if (s->s[i + j] != o->s[j]) match = false;
        if (match && o->len > 0) {
            out += n ? n->s : "";
            i += o->len;
        } else {
            out += s->s[i++];
        }
    }
    return val_make_str(out.c_str());
}

// substr(s, start, len): 子串
static Value s_substr(int argc, Value* argv, EvalCtx* ctx) {
    (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    if (!s) return val_make_str("");
    int start = (int)val_as_int(argv[1]);
    int len = (argc >= 3) ? (int)val_as_int(argv[2]) : (s->len - start);
    if (start < 0) start = 0;
    if (start > s->len) start = s->len;
    if (len < 0) len = 0;
    if (start + len > s->len) len = s->len - start;
    return val_make_str_n(s->s + start, len);
}

// ---------------- 数学函数（定点） ----------------
static Value m_sin(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(fx::fx_sin(val_as_fx(argv[0])));
}
static Value m_cos(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(fx::fx_cos(val_as_fx(argv[0])));
}
static Value m_sqrt(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    fx::fix x = val_as_fx(argv[0]);
    if (x < 0) x = 0;
    return val_make_fx(fx::fx_sqrt(x));
}
static Value m_pow(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(fx::fx_pow(val_as_fx(argv[0]), val_as_fx(argv[1])));
}
static Value m_floor(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(fx::fx_floor(val_as_fx(argv[0])));
}
static Value m_ceil(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(fx::fx_ceil(val_as_fx(argv[0])));
}
static Value m_tan(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    fx::fix x = val_as_fx(argv[0]);
    fx::fix c = fx::fx_cos(x);
    if (c == 0) return val_make_fx(0);
    return val_make_fx(fx::fx_div(fx::fx_sin(x), c));
}

// upper(s): 转大写（仅 ASCII 字母）
static Value s_upper(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    if (!s) return val_make_str("");
    String out;
    for (int i = 0; i < s->len; i++) {
        char c = s->s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out += c;
    }
    return val_make_str(out.c_str());
}

// lower(s): 转小写
static Value s_lower(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    if (!s) return val_make_str("");
    String out;
    for (int i = 0; i < s->len; i++) {
        char c = s->s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        out += c;
    }
    return val_make_str(out.c_str());
}

// trim(s): 去掉首尾空白
static Value s_trim(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    if (!s) return val_make_str("");
    int lo = 0, hi = s->len;
    while (lo < hi && (s->s[lo] == ' ' || s->s[lo] == '\t' || s->s[lo] == '\n')) lo++;
    while (hi > lo && (s->s[hi-1] == ' ' || s->s[hi-1] == '\t' || s->s[hi-1] == '\n')) hi--;
    return val_make_str_n(s->s + lo, hi - lo);
}

// starts_with(s, sub) / ends_with(s, sub)
static Value s_starts(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    StrObj* sub = val_as_str(argv[1]);
    if (!s || !sub || sub->len > s->len) return val_make_bool(false);
    for (int i = 0; i < sub->len; i++)
        if (s->s[i] != sub->s[i]) return val_make_bool(false);
    return val_make_bool(true);
}
static Value s_ends(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    StrObj* sub = val_as_str(argv[1]);
    if (!s || !sub || sub->len > s->len) return val_make_bool(false);
    int off = s->len - sub->len;
    for (int i = 0; i < sub->len; i++)
        if (s->s[off + i] != sub->s[i]) return val_make_bool(false);
    return val_make_bool(true);
}

// char_at(s, i): 取第 i 个字符（单字符字符串）
static Value s_charat(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    int i = (int)val_as_int(argv[1]);
    if (!s || i < 0 || i >= s->len) return val_make_str("");
    return val_make_str_n(s->s + i, 1);
}

void stdlib_register_all(Env* env) {
    if (!env) return;
    env_define(env, "split",   val_make_builtin("split",   s_split));
    env_define(env, "join",    val_make_builtin("join",    s_join));
    env_define(env, "find",    val_make_builtin("find",    s_find));
    env_define(env, "replace", val_make_builtin("replace", s_replace));
    env_define(env, "substr",  val_make_builtin("substr",  s_substr));
    env_define(env, "sin",      val_make_builtin("sin",     m_sin));
    env_define(env, "cos",      val_make_builtin("cos",     m_cos));
    env_define(env, "sqrt",     val_make_builtin("sqrt",    m_sqrt));
    env_define(env, "pow",      val_make_builtin("pow",     m_pow));
    env_define(env, "floor",    val_make_builtin("floor",   m_floor));
    env_define(env, "ceil",     val_make_builtin("ceil",    m_ceil));
    env_define(env, "tan",      val_make_builtin("tan",     m_tan));
    env_define(env, "upper",    val_make_builtin("upper",   s_upper));
    env_define(env, "lower",    val_make_builtin("lower",   s_lower));
    env_define(env, "trim",     val_make_builtin("trim",    s_trim));
    env_define(env, "starts_with", val_make_builtin("starts_with", s_starts));
    env_define(env, "ends_with",   val_make_builtin("ends_with",   s_ends));
    env_define(env, "char_at",  val_make_builtin("char_at", s_charat));
    env_define(env, "PI",       val_make_fx(fx::FX_PI));
}

int stdlib_self_test() {
    int fails = 0;

    // split
    {
        StrObj* tmp = (StrObj*)val_make_str("a,b,c").obj;
        Value s; s.type = V_OBJ; s.i = 0; s.obj = tmp;
        StrObj* sep = (StrObj*)val_make_str(",").obj;
        Value sepv; sepv.type = V_OBJ; sepv.i = 0; sepv.obj = sep;
        Value argv[2] = { s, sepv };
        Value r = s_split(2, argv, 0);
        ArrObj* a = val_as_arr(r);
        if (!a || a->len != 3) fails++;
        if (a && val_as_str(a->items[0]) && strcmp(val_as_str(a->items[0])->s, "a") != 0) fails++;
        val_drop(r); val_drop(s); val_drop(sepv);
    }

    // find
    {
        StrObj* tmp = (StrObj*)val_make_str("hello world").obj;
        Value s; s.type = V_OBJ; s.i = 0; s.obj = tmp;
        StrObj* sub = (StrObj*)val_make_str("world").obj;
        Value v; v.type = V_OBJ; v.i = 0; v.obj = sub;
        Value argv[2] = { s, v };
        Value r = s_find(2, argv, 0);
        if (val_as_int(r) != 6) fails++;
        val_drop(r); val_drop(s); val_drop(v);
    }

    // replace
    {
        StrObj* tmp = (StrObj*)val_make_str("a b a").obj;
        Value s; s.type = V_OBJ; s.i = 0; s.obj = tmp;
        StrObj* o = (StrObj*)val_make_str("a").obj;
        Value ov; ov.type = V_OBJ; ov.i = 0; ov.obj = o;
        StrObj* n = (StrObj*)val_make_str("x").obj;
        Value nv; nv.type = V_OBJ; nv.i = 0; nv.obj = n;
        Value argv[3] = { s, ov, nv };
        Value r = s_replace(3, argv, 0);
        StrObj* rs = val_as_str(r);
        if (!rs || strcmp(rs->s, "x b x") != 0) fails++;
        val_drop(r); val_drop(s); val_drop(ov); val_drop(nv);
    }

    // sqrt(4)=2
    {
        Value x = val_make_int(4);
        Value argv[1] = { x };
        Value r = m_sqrt(1, argv, 0);
        if (fx::fixtoi((fx::fix)r.i) != 2) fails++;
        val_drop(r); val_drop(x);
    }

    return fails;
}

} // namespace minilang
} // namespace nefu

