// ============================================================================
// nefu::minilang —— 树遍历解释器（实现）
// ============================================================================
#include "eval.h"
#include "parser.h"
#include "builtin.h"
#include "stdlib.h"
#include <string.h>

namespace nefu {
namespace minilang {

// 运行时错误：置位并返回 null
static Value eval_fail(EvalCtx* ctx, const char* msg) {
    if (!ctx->error) { ctx->error = true; ctx->errmsg = msg; }
    return val_make_null();
}

// 二元数值/字符串运算
static Value bin_op(EvalCtx* ctx, int op, Value l, Value r) {
    // 字符串拼接：+ 且至少一边是字符串
    if (op == OP_ADD) {
        StrObj* ls = val_as_str(l);
        StrObj* rs = val_as_str(r);
        if (ls || rs) {
            StrObj* a = val_to_strobj(l);
            StrObj* b = val_to_strobj(r);
            String out = String(a->s);
            out += b->s;
            Value va; va.type = V_OBJ; va.i = 0; va.obj = a; val_drop(va);
            Value vb; vb.type = V_OBJ; vb.i = 0; vb.obj = b; val_drop(vb);
            return val_make_str(out.c_str());
        }
    }
    // 比较运算
    if (op == OP_EQ || op == OP_NEQ || op == OP_LT || op == OP_GT ||
        op == OP_LE || op == OP_GE) {
        fx::fix lf = val_as_fx(l), rf = val_as_fx(r);
        int c = 0;
        if (lf < rf) c = -1; else if (lf > rf) c = 1;
        bool res = false;
        switch (op) {
        case OP_EQ: res = (c == 0); break;
        case OP_NEQ: res = (c != 0); break;
        case OP_LT: res = (c < 0); break;
        case OP_GT: res = (c > 0); break;
        case OP_LE: res = (c <= 0); break;
        case OP_GE: res = (c >= 0); break;
        }
        return val_make_bool(res);
    }
    // 算术：两边都是整数走整数，否则走定点
    bool both_int = (l.type == V_INT && r.type == V_INT);
    if (both_int) {
        int64_t a = l.i, b = r.i;
        switch (op) {
        case OP_ADD: return val_make_int(a + b);
        case OP_SUB: return val_make_int(a - b);
        case OP_MUL: return val_make_int(a * b);
        case OP_DIV: return b == 0 ? eval_fail(ctx, "division by zero") : val_make_int(a / b);
        case OP_MOD: return b == 0 ? eval_fail(ctx, "mod by zero") : val_make_int(a % b);
        }
    }
    fx::fix a = val_as_fx(l), b = val_as_fx(r);
    switch (op) {
    case OP_ADD: return val_make_fx(a + b);
    case OP_SUB: return val_make_fx(a - b);
    case OP_MUL: return val_make_fx(fx::fx_mul(a, b));
    case OP_DIV: return val_make_fx(fx::fx_div(a, b));
    case OP_MOD: {
        // 定点取模：a - floor(a/b)*b
        fx::fix q = fx::fx_div(a, b);
        q = fx::fx_floor(q);
        return val_make_fx(a - fx::fx_mul(q, b));
    }
    }
    return eval_fail(ctx, "bad arithmetic op");
}

// 执行函数调用（闭包或内置）
static Value do_call(EvalCtx* ctx, Value callee, List<Node*>& args, Env* env);

Value eval_expr(EvalCtx* ctx, Node* e, Env* env) {
    if (!e || ctx->error) return val_make_null();
    switch (e->kind) {
    case N_LIT: {
        LitNode* n = (LitNode*)e;
        switch (n->tag) {
        case LIT_NULL: return val_make_null();
        case LIT_BOOL: return val_make_bool(n->i != 0);
        case LIT_INT:  return val_make_int(n->i);
        case LIT_FLOAT: return val_make_fx(n->f);
        case LIT_STR:  return val_make_str(n->s);
        }
        return val_make_null();
    }
    case N_VAR: {
        VarNode* v = (VarNode*)e;
        Value* p = env_lookup(env, v->name);
        if (!p) return eval_fail(ctx, "undefined variable");
        return val_dup(*p);
    }
    case N_BIN: {
        BinNode* b = (BinNode*)e;
        // 短路 and/or
        if (b->op == OP_AND) {
            Value l = eval_expr(ctx, b->l, env);
            if (ctx->error) { val_drop(l); return val_make_null(); }
            if (!val_is_truthy(l)) { val_drop(l); return val_make_bool(false); }
            val_drop(l);
            return eval_expr(ctx, b->r, env);
        }
        if (b->op == OP_OR) {
            Value l = eval_expr(ctx, b->l, env);
            if (ctx->error) { val_drop(l); return val_make_null(); }
            if (val_is_truthy(l)) { Value r = val_make_bool(true); val_drop(l); return r; }
            val_drop(l);
            return eval_expr(ctx, b->r, env);
        }
        Value l = eval_expr(ctx, b->l, env);
        if (ctx->error) { val_drop(l); return val_make_null(); }
        Value r = eval_expr(ctx, b->r, env);
        if (ctx->error) { val_drop(l); val_drop(r); return val_make_null(); }
        Value res = bin_op(ctx, b->op, l, r);
        val_drop(l); val_drop(r);
        return res;
    }
    case N_UNARY: {
        UnaryNode* u = (UnaryNode*)e;
        Value v = eval_expr(ctx, u->e, env);
        if (ctx->error) { val_drop(v); return val_make_null(); }
        Value res;
        if (u->op == OP_NEG) {
            if (v.type == V_INT) res = val_make_int(-v.i);
            else res = val_make_fx((fx::fix)(-(fx::fix)val_as_fx(v)));
        } else { // OP_NOT
            res = val_make_bool(!val_is_truthy(v));
        }
        val_drop(v);
        return res;
    }
    case N_ASSIGN: {
        AssignNode* a = (AssignNode*)e;
        Value v = eval_expr(ctx, a->value, env);
        if (ctx->error) { val_drop(v); return val_make_null(); }
        if (a->target->kind == N_VAR) {
            VarNode* t = (VarNode*)a->target;
            if (!env_assign(env, t->name, v)) {
                eval_fail(ctx, "assignment to undefined variable");
                val_drop(v);
                return val_make_null();
            }
        } else if (a->target->kind == N_INDEX) {
            IndexNode* ix = (IndexNode*)a->target;
            Value obj = eval_expr(ctx, ix->obj, env);
            Value idx = eval_expr(ctx, ix->idx, env);
            ArrObj* arr = val_as_arr(obj);
            if (!arr) { eval_fail(ctx, "index target is not an array"); }
            else if (!arr_set(arr, (int)val_as_int(idx), v)) {
                eval_fail(ctx, "index assignment out of range");
            }
            val_drop(obj); val_drop(idx);
        }
        val_drop(v);
        return v;
    }
    case N_ARRAY: {
        ArrayNode* an = (ArrayNode*)e;
        Value arr = val_make_arr(an->elems.size());
        ArrObj* a = val_as_arr(arr);
        for (int i = 0; i < an->elems.size(); i++) {
            Value el = eval_expr(ctx, an->elems[i], env);
            if (ctx->error) { val_drop(arr); val_drop(el); return val_make_null(); }
            arr_push(a, el);
            val_drop(el);
        }
        return arr;
    }
    case N_INDEX: {
        IndexNode* ix = (IndexNode*)e;
        Value obj = eval_expr(ctx, ix->obj, env);
        if (ctx->error) { val_drop(obj); return val_make_null(); }
        Value idx = eval_expr(ctx, ix->idx, env);
        if (ctx->error) { val_drop(obj); val_drop(idx); return val_make_null(); }
        ArrObj* arr = val_as_arr(obj);
        Value res;
        if (!arr) { eval_fail(ctx, "index on non-array"); res = val_make_null(); }
        else res = arr_get(arr, (int)val_as_int(idx));
        val_drop(obj); val_drop(idx);
        return res;
    }
    case N_CALL: {
        CallNode* c = (CallNode*)e;
        Value callee = eval_expr(ctx, c->callee, env);
        if (ctx->error) { val_drop(callee); return val_make_null(); }
        Value res = do_call(ctx, callee, c->args, env);
        val_drop(callee);
        return res;
    }
    default:
        return eval_fail(ctx, "cannot evaluate node");
    }
}

// 函数调用：闭包 or 内置
static Value do_call(EvalCtx* ctx, Value callee, List<Node*>& args, Env* env) {
    ClosureObj* clo = val_as_closure(callee);
    BuiltinObj* bi  = val_as_builtin(callee);

    if (bi) {
        // 求值实参
        Value* argv = new Value[args.size() > 0 ? args.size() : 1];
        for (int i = 0; i < args.size(); i++) {
            argv[i] = eval_expr(ctx, args[i], env);
            if (ctx->error) {
                for (int j = 0; j < i; j++) val_drop(argv[j]);
                delete[] argv;
                return val_make_null();
            }
        }
        Value r = bi->fn(args.size(), argv, ctx);
        for (int i = 0; i < args.size(); i++) val_drop(argv[i]);
        delete[] argv;
        return r;
    }

    if (clo) {
        FuncNode* fn = clo->fn;
        // 新栈帧：父环境 = 闭包捕获的环境
        Env* frame = env_new(clo->env);
        // 绑定形参
        int n = fn->params.size();
        if (n > args.size()) n = args.size();
        for (int i = 0; i < n; i++) {
            Value a = eval_expr(ctx, args[i], env);
            if (ctx->error) { env_release(frame); val_drop(a); return val_make_null(); }
            env_define(frame, fn->params[i], a);  // 所有权转入
        }
        // 执行函数体（块）
        ctx->returning = false;
        ctx->retval = val_make_null();
        eval_stmt(ctx, fn->body, frame);
        Value r;
        if (ctx->returning) {
            r = ctx->retval;     // 转移所有权
            ctx->retval = val_make_null();
        } else {
            r = val_make_null();
        }
        ctx->returning = false;
        env_release(frame);
        return r;
    }

    return eval_fail(ctx, "calling a non-function");
}

// 执行语句
void eval_stmt(EvalCtx* ctx, Node* s, Env* env) {
    if (!s || ctx->error || ctx->returning) return;
    switch (s->kind) {
    case N_EXPR_STMT: {
        ExprStmt* es = (ExprStmt*)s;
        Value v = eval_expr(ctx, es->e, env);
        val_drop(ctx->last_expr);
        ctx->last_expr = v;
        break;
    }
    case N_LET: {
        LetStmt* ls = (LetStmt*)s;
        Value init = ls->init ? eval_expr(ctx, ls->init, env) : val_make_null();
        if (ctx->error) { val_drop(init); return; }
        env_define(env, ls->name, init);   // 所有权转入
        break;
    }
    case N_BLOCK: {
        BlockStmt* b = (BlockStmt*)s;
        Env* inner = env_new(env);
        for (int i = 0; i < b->stmts.size(); i++) {
            eval_stmt(ctx, b->stmts[i], inner);
            if (ctx->returning || ctx->error) break;
        }
        env_release(inner);
        break;
    }
    case N_IF: {
        IfStmt* is = (IfStmt*)s;
        Value c = eval_expr(ctx, is->cond, env);
        if (ctx->error) { val_drop(c); return; }
        bool t = val_is_truthy(c);
        val_drop(c);
        if (t) eval_stmt(ctx, is->thenb, env);
        else if (is->elseb) eval_stmt(ctx, is->elseb, env);
        break;
    }
    case N_WHILE: {
        WhileStmt* ws = (WhileStmt*)s;
        while (!ctx->error && !ctx->returning) {
            Value c = eval_expr(ctx, ws->cond, env);
            if (ctx->error) { val_drop(c); break; }
            if (!val_is_truthy(c)) { val_drop(c); break; }
            val_drop(c);
            eval_stmt(ctx, ws->body, env);
        }
        break;
    }
    case N_FOR: {
        ForStmt* fs = (ForStmt*)s;
        Env* inner = env_new(env);
        if (fs->init) eval_stmt(ctx, fs->init, inner);
        while (!ctx->error && !ctx->returning) {
            if (fs->cond) {
                Value c = eval_expr(ctx, fs->cond, inner);
                if (ctx->error) { val_drop(c); break; }
                if (!val_is_truthy(c)) { val_drop(c); break; }
                val_drop(c);
            }
            eval_stmt(ctx, fs->body, inner);
            if (ctx->returning || ctx->error) break;
            if (fs->post) {
                Value p = eval_expr(ctx, fs->post, inner);
                val_drop(p);
            }
        }
        env_release(inner);
        break;
    }
    case N_RETURN: {
        ReturnStmt* rs = (ReturnStmt*)s;
        Value v = rs->e ? eval_expr(ctx, rs->e, env) : val_make_null();
        if (ctx->error) { val_drop(v); return; }
        ctx->returning = true;
        ctx->retval = v;
        break;
    }
    case N_FUNC: {
        FuncNode* fn = (FuncNode*)s;
        // 顶层函数定义：在当前环境绑定一个闭包（捕获当前环境）
        Value clo = val_make_closure(fn, ctx->keep_prog, env);
        // 注意：prog 所有权由调用方持有；这里 prog=0，闭包不延长 AST 寿命
        // （顶层程序在整个 REPL 期间存活，见 minilang_run）。
        env_define(env, fn->name, clo);
        break;
    }
    default:
        break;
    }
}

// 执行整棵程序
Value eval_program(EvalCtx* ctx, ProgramObj* prog, Env* global) {
    Value last = val_make_null();
    for (int i = 0; i < prog->stmts.size() && !ctx->error; i++) {
        Node* s = prog->stmts[i];
        eval_stmt(ctx, s, global);
        // 最近表达式语句的值已由 eval_stmt 存入 ctx->last_expr（不重复求值）
        val_drop(last);
        last = val_dup(ctx->last_expr);
        if (ctx->returning) {
            // 顶层 return 视为结束
            Value r = ctx->retval;
            ctx->retval = val_make_null();
            ctx->returning = false;
            val_drop(last);
            return r;
        }
    }
    return last;
}

// 便捷入口：解析 + 运行
Value minilang_run(const char* source, Env* global_env, String* errmsg_out) {
    String err;
    Value progv = minilang_parse(source, &err);
    if (val_is_null(progv)) {
        if (errmsg_out) *errmsg_out = err;
        return val_make_null();
    }
    ProgramObj* prog = val_as_program(progv);

    Env* g = global_env;
    bool created = false;
    if (!g) { g = env_new(0); builtin_register_all(g); stdlib_register_all(g); created = true; }

    EvalCtx ctx;
    ctx.global = g;
    ctx.keep_prog = prog;
    Value r = eval_program(&ctx, prog, g);

    if (ctx.error) {
        if (errmsg_out) *errmsg_out = ctx.errmsg;
        val_drop(r);
        r = val_make_null();
    }
    val_drop(progv);   // 释放 AST（仍被闭包引用则存活）
    if (created) env_release(g);
    return r;
}

// 自测试：运行多段迷你语言代码，验证结果。
static int check_run(const char* src, int64_t expect) {
    String err;
    Value r = minilang_run(src, 0, &err);
    if (val_is_null(r) && expect != -9999) {
        // 出错
        return 1;
    }
    int64_t got = val_as_int(r);
    val_drop(r);
    if (got != expect) return 1;
    return 0;
}

int eval_self_test() {
    int fails = 0;

    // 算术
    fails += check_run("1+2*3", 7);
    fails += check_run("(1+2)*3", 9);
    fails += check_run("10/3", 3);
    fails += check_run("7%3", 1);
    fails += check_run("-5+2", -3);

    // 变量
    fails += check_run("let x = 10; let y = 20; x + y;", 30);
    fails += check_run("let x = 1; x = x + 5; x;", 6);

    // if/else
    fails += check_run("let a = 0; if (1) { a = 1; } else { a = 2; } a;", 1);
    fails += check_run("let a = 0; if (0) { a = 1; } else { a = 2; } a;", 2);

    // while
    fails += check_run("let i = 0; let s = 0; while (i < 5) { s = s + i; i = i + 1; } s;", 10);

    // for
    fails += check_run("let s = 0; for (let i = 0; i < 4; i = i + 1) { s = s + i; } s;", 6);

    // 递归斐波那契
    fails += check_run("fn fib(n){ if (n < 2) { return n; } return fib(n-1)+fib(n-2); } fib(10);", 55);

    // 闭包：返回一个捕获 x 的函数
    fails += check_run(
        "fn mk(){ let x = 42; fn get(){ return x; } return get; } "
        "let g = mk(); g();", 42);

    // 数组
    fails += check_run("let a = [10,20,30]; a[0] + a[2];", 40);
    fails += check_run("let a = [1,2,3]; a[1] = 99; a[1];", 99);

    // 内置函数
    fails += check_run("len([1,2,3,4]);", 4);
    fails += check_run("sum([1,2,3,4]);", 10);
    fails += check_run("max(3,7,2);", 7);
    fails += check_run("abs(-9);", 9);
    fails += check_run("range(5)[4];", 4);

    // 布尔逻辑
    fails += check_run("1 < 2;", 1);
    fails += check_run("2 < 1;", 0);
    fails += check_run("1 == 1;", 1);
    fails += check_run("true and false;", 0);
    fails += check_run("true or false;", 1);

    return fails;
}

} // namespace minilang
} // namespace nefu







