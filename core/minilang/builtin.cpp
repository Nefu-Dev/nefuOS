// ============================================================================
// nefu::minilang —— 内置函数（实现）
// ============================================================================
#include "builtin.h"
#include <string.h>

namespace nefu {
namespace minilang {

// 输出槽（8KB 环形截断）
char g_minilang_out[8192];
int  g_minilang_out_len = 0;

void minilang_out_reset() {
    g_minilang_out_len = 0;
    g_minilang_out[0] = 0;
}

void minilang_out_write(const char* s) {
    if (!s) return;
    for (; *s; s++) {
        if (g_minilang_out_len < 8191) {
            g_minilang_out[g_minilang_out_len++] = *s;
        }
    }
    g_minilang_out[g_minilang_out_len] = 0;
}

// ---------------- 内置函数实现 ----------------
// print(a, b, ...) 把各值转字符串用空格连接后换行输出，返回 null
static Value b_print(int argc, Value* argv, EvalCtx* ctx) {
    (void)ctx;
    for (int i = 0; i < argc; i++) {
        StrObj* s = val_to_strobj(argv[i]);
        if (i > 0) minilang_out_write(" ");
        minilang_out_write(s->s);
        Value tmp; tmp.type = V_OBJ; tmp.i = 0; tmp.obj = s;
        val_drop(tmp);
    }
    minilang_out_write("\n");
    return val_make_null();
}

// len(x)：字符串长度 / 数组长度
static Value b_len(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    Value v = argv[0];
    StrObj* s = val_as_str(v);
    if (s) return val_make_int(s->len);
    ArrObj* a = val_as_arr(v);
    if (a) return val_make_int(a->len);
    return val_make_int(0);
}

// type(x) 返回类型字符串
static Value b_type(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_str(val_type_name(argv[0]));
}

// str(x) 把值转字符串
static Value b_str(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_to_strobj(argv[0]);
    Value v; v.type = V_OBJ; v.i = 0; v.obj = s;
    return v;
}

// int(x) 转整数
static Value b_int(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_int(val_as_int(argv[0]));
}

// float(x) 转定点数
static Value b_float(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_fx(val_as_fx(argv[0]));
}

// abs(x)
static Value b_abs(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    Value v = argv[0];
    if (v.type == V_INT) {
        int64_t n = v.i;
        return val_make_int(n < 0 ? -n : n);
    }
    if (v.type == V_FX) {
        fx::fix f = (fx::fix)v.i;
        return val_make_fx(fx::fx_abs(f));
    }
    return val_make_null();
}

// max(a, b, ...) / min(a, b, ...)
static Value b_max(int argc, Value* argv, EvalCtx* ctx) {
    (void)ctx;
    if (argc == 0) return val_make_null();
    Value best = argv[0];
    for (int i = 1; i < argc; i++) {
        if (val_as_fx(argv[i]) > val_as_fx(best)) best = argv[i];
    }
    return val_dup(best);
}
static Value b_min(int argc, Value* argv, EvalCtx* ctx) {
    (void)ctx;
    if (argc == 0) return val_make_null();
    Value best = argv[0];
    for (int i = 1; i < argc; i++) {
        if (val_as_fx(argv[i]) < val_as_fx(best)) best = argv[i];
    }
    return val_dup(best);
}

// sum(arr) 求数组元素和（整数）
static Value b_sum(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    if (!a) return val_make_int(0);
    int64_t acc = 0;
    for (int i = 0; i < a->len; i++) acc += val_as_int(a->items[i]);
    return val_make_int(acc);
}

// sort(arr) 原地升序排序，返回 arr 本身
static Value b_sort(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    if (!a) return val_make_null();
    // 简单插入排序（按定点数比较）
    for (int i = 1; i < a->len; i++) {
        Value key = a->items[i];
        fx::fix kf = val_as_fx(key);
        int j = i - 1;
        while (j >= 0 && val_as_fx(a->items[j]) > kf) {
            a->items[j + 1] = a->items[j];
            j--;
        }
        a->items[j + 1] = key;
    }
    return val_dup(argv[0]);
}

// range(n) 或 range(lo, hi)：返回整数数组 [lo, hi)（或 [0,n)）
static Value b_range(int argc, Value* argv, EvalCtx* ctx) {
    (void)ctx;
    int64_t lo = 0, hi = 0;
    if (argc == 1) { hi = val_as_int(argv[0]); }
    else if (argc >= 2) { lo = val_as_int(argv[0]); hi = val_as_int(argv[1]); }
    Value arr = val_make_arr(4);
    ArrObj* a = val_as_arr(arr);
    for (int64_t i = lo; i < hi; i++) arr_push(a, val_make_int(i));
    return arr;
}

// push(arr, x) 把 x 追加到数组末尾，返回 arr
static Value b_push(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    if (!a) return val_make_null();
    arr_push(a, val_dup(argv[1]));
    return val_dup(argv[0]);
}

// pop(arr) 移除并返回数组末尾元素
static Value b_pop(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    if (!a || a->len <= 0) return val_make_null();
    Value v = a->items[a->len - 1];
    a->len--;
    return v;   // 所有权转出
}

// chr(n) 整数 -> 单字符字符串
static Value b_chr(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    char buf[2] = { (char)(int)val_as_int(argv[0]), 0 };
    return val_make_str(buf);
}

// ord(s) 字符串首字符 -> 整数
static Value b_ord(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    StrObj* s = val_as_str(argv[0]);
    if (!s || s->len < 1) return val_make_int(0);
    return val_make_int((unsigned char)s->s[0]);
}

// reverse(arr) 返回反转后的新数组
static Value b_reverse(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    ArrObj* a = val_as_arr(argv[0]);
    if (!a) return val_make_null();
    Value out = val_make_arr(4);
    ArrObj* o = val_as_arr(out);
    for (int i = a->len - 1; i >= 0; i--) arr_push(o, val_dup(a->items[i]));
    return out;
}

// floor(x) / ceil(x) / round(x) 定点数取整
static Value b_floor(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_int(fx::fixtoi(fx::fx_floor(val_as_fx(argv[0]))));
}
static Value b_ceil(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    return val_make_int(fx::fixtoi(fx::fx_ceil(val_as_fx(argv[0]))));
}
static Value b_round(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    fx::fix f = val_as_fx(argv[0]);
    fx::fix half = 32768;
    if (f < 0) f -= half; else f += half;
    return val_make_int(fx::fixtoi(f));
}

// min2 / max2 二元（保留给教学）
static Value b_clamp(int argc, Value* argv, EvalCtx* ctx) {
    (void)argc; (void)ctx;
    fx::fix v = val_as_fx(argv[0]);
    fx::fix lo = val_as_fx(argv[1]);
    fx::fix hi = val_as_fx(argv[2]);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return val_make_fx(v);
}

// 注册所有内置函数
void builtin_register_all(Env* env) {
    if (!env) return;
    env_define(env, "print",  val_make_builtin("print",  b_print));
    env_define(env, "len",    val_make_builtin("len",    b_len));
    env_define(env, "type",    val_make_builtin("type",   b_type));
    env_define(env, "str",    val_make_builtin("str",    b_str));
    env_define(env, "int",    val_make_builtin("int",    b_int));
    env_define(env, "float",  val_make_builtin("float",  b_float));
    env_define(env, "abs",    val_make_builtin("abs",    b_abs));
    env_define(env, "max",    val_make_builtin("max",    b_max));
    env_define(env, "min",    val_make_builtin("min",    b_min));
    env_define(env, "sum",    val_make_builtin("sum",    b_sum));
    env_define(env, "sort",   val_make_builtin("sort",   b_sort));
    env_define(env, "range",  val_make_builtin("range",  b_range));
    env_define(env, "push",   val_make_builtin("push",   b_push));
    env_define(env, "pop",    val_make_builtin("pop",    b_pop));
    env_define(env, "chr",    val_make_builtin("chr",    b_chr));
    env_define(env, "ord",    val_make_builtin("ord",    b_ord));
    env_define(env, "reverse", val_make_builtin("reverse", b_reverse));
    env_define(env, "floor",  val_make_builtin("floor",  b_floor));
    env_define(env, "ceil",   val_make_builtin("ceil",   b_ceil));
    env_define(env, "round",  val_make_builtin("round",  b_round));
    env_define(env, "clamp",  val_make_builtin("clamp",  b_clamp));
}

// 自测试
int builtin_self_test() {
    int fails = 0;
    Env* g = env_new(0);
    builtin_register_all(g);

    // len
    {
        Value s = val_make_str("hello");
        Value argv[1] = { s };
        Value r = b_len(1, argv, 0);
        if (val_as_int(r) != 5) fails++;
        val_drop(r); val_drop(s);
    }
    // range + sum
    {
        Value n = val_make_int(5);
        Value argv[1] = { n };
        Value r = b_range(1, argv, 0);
        ArrObj* a = val_as_arr(r);
        if (!a || a->len != 5) fails++;
        Value sa[1] = { r };
        Value s = b_sum(1, sa, 0);
        if (val_as_int(s) != 10) fails++;   // 0+1+2+3+4
        val_drop(s); val_drop(r);
    }
    // max/min
    {
        Value a = val_make_int(3), b = val_make_int(9), c = val_make_int(-2);
        Value argv[3] = { a, b, c };
        Value m = b_max(3, argv, 0);
        if (val_as_int(m) != 9) fails++;
        val_drop(m);
        Value n = b_min(3, argv, 0);
        if (val_as_int(n) != -2) fails++;
        val_drop(n);
        val_drop(a); val_drop(b); val_drop(c);
    }
    // sort
    {
        Value arr = val_make_arr();
        ArrObj* a = val_as_arr(arr);
        arr_push(a, val_make_int(3));
        arr_push(a, val_make_int(1));
        arr_push(a, val_make_int(2));
        Value argv[1] = { arr };
        Value r = b_sort(1, argv, 0);
        ArrObj* ra = val_as_arr(r);
        if (!ra || val_as_int(ra->items[0]) != 1) fails++;
        if (val_as_int(ra->items[2]) != 3) fails++;
        val_drop(r); val_drop(arr);
    }
    // print 输出槽
    {
        minilang_out_reset();
        Value s = val_make_str("hi");
        Value argv[1] = { s };
        b_print(1, argv, 0);
        if (g_minilang_out_len < 2) fails++;
        if (g_minilang_out[0] != 'h') fails++;
        val_drop(s);
    }

    env_release(g);
    return fails;
}

} // namespace minilang
} // namespace nefu

