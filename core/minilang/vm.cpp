// ============================================================================
// nefu::minilang —— 栈式虚拟机（实现）
// ============================================================================
#include "vm.h"
#include "stdlib.h"
#include "builtin.h"
#include "parser.h"
#include <string.h>

namespace nefu {
namespace minilang {

Vm::Vm(Env* g) : global(g), error(false), errmsg() {}
Vm::~Vm() {
    for (int i = 0; i < stack.size(); i++) val_drop(stack[i]);
    for (int i = 0; i < frames.size(); i++) env_release(frames[i].env);
}

static int rd_i32(const CodeChunk* cc, int ip) {
    const List<uint8_t>& c = cc->code;
    return (int)c[ip] | ((int)c[ip+1] << 8) | ((int)c[ip+2] << 16) | ((int)c[ip+3] << 24);
}

static bool vm_fail(Vm* v, const char* msg) {
    if (!v->error) { v->error = true; v->errmsg = msg; }
    return false;
}

// 弹出栈顶（调用方负责 drop）
static Value pop(Vm* v) {
    if (v->stack.size() == 0) { vm_fail(v, "stack underflow"); return val_make_null(); }
    Value r = v->stack[v->stack.size() - 1];
    v->stack.pop();
    return r;
}

static void push(Vm* v, Value x) { v->stack.push(x); }

// VM 内二元运算（与 eval.cpp 等价）
static Value vm_bin(Vm* v, int opc, Value l, Value r) {
    if (opc == BC_ADD) {
        if (val_as_str(l) || val_as_str(r)) {
            StrObj* a = val_to_strobj(l);
            StrObj* b = val_to_strobj(r);
            String out = String(a->s); out += b->s;
            Value va; va.type = V_OBJ; va.i = 0; va.obj = a; val_drop(va);
            Value vb; vb.type = V_OBJ; vb.i = 0; vb.obj = b; val_drop(vb);
            return val_make_str(out.c_str());
        }
    }
    if (opc >= BC_EQ && opc <= BC_GE) {
        fx::fix lf = val_as_fx(l), rf = val_as_fx(r);
        int c = (lf < rf) ? -1 : (lf > rf ? 1 : 0);
        bool res = false;
        switch (opc) {
        case BC_EQ:  res = (c == 0); break;
        case BC_NEQ: res = (c != 0); break;
        case BC_LT:  res = (c < 0); break;
        case BC_GT:  res = (c > 0); break;
        case BC_LE:  res = (c <= 0); break;
        case BC_GE:  res = (c >= 0); break;
        }
        return val_make_bool(res);
    }
    bool both_int = (l.type == V_INT && r.type == V_INT);
    if (both_int) {
        int64_t a = l.i, b = r.i;
        switch (opc) {
        case BC_ADD: return val_make_int(a + b);
        case BC_SUB: return val_make_int(a - b);
        case BC_MUL: return val_make_int(a * b);
        case BC_DIV: return b == 0 ? (vm_fail(v,"division by zero"), val_make_null()) : val_make_int(a / b);
        case BC_MOD: return b == 0 ? (vm_fail(v,"mod by zero"), val_make_null()) : val_make_int(a % b);
        }
    }
    fx::fix a = val_as_fx(l), b = val_as_fx(r);
    switch (opc) {
    case BC_ADD: return val_make_fx(a + b);
    case BC_SUB: return val_make_fx(a - b);
    case BC_MUL: return val_make_fx(fx::fx_mul(a, b));
    case BC_DIV: return val_make_fx(fx::fx_div(a, b));
    case BC_MOD: {
        fx::fix q = fx::fx_floor(fx::fx_div(a, b));
        return val_make_fx(a - fx::fx_mul(q, b));
    }
    }
    return val_make_null();
}

Value Vm::run(CodeChunk* cc) {
    // 主帧
    VmFrame main;
    main.chunk = cc;
    main.ip = 0;
    main.env = global;
    frames.push(main);

    while (!error && frames.size() > 0) {
        VmFrame* fr = &frames[frames.size() - 1];
        const CodeChunk* cur = fr->chunk;
        const List<uint8_t>& code = cur->code;
        uint8_t op = code[fr->ip++];

        switch (op) {
        case BC_CONST: {
            int i = rd_i32(cur, fr->ip); fr->ip += 4;
            push(this, val_dup(cur->consts[i]));
            break;
        }
        case BC_TRUE:  push(this, val_make_bool(true)); break;
        case BC_FALSE: push(this, val_make_bool(false)); break;
        case BC_NULL:  push(this, val_make_null()); break;

        case BC_ADD: case BC_SUB: case BC_MUL: case BC_DIV: case BC_MOD:
        case BC_EQ: case BC_NEQ: case BC_LT: case BC_GT: case BC_LE: case BC_GE: {
            Value r = pop(this);
            Value l = pop(this);
            Value res = vm_bin(this, op, l, r);
            val_drop(l); val_drop(r);
            push(this, res);
            break;
        }
        case BC_NOT: {
            Value v = pop(this);
            Value r = val_make_bool(!val_is_truthy(v));
            val_drop(v);
            push(this, r);
            break;
        }
        case BC_NEG: {
            Value v = pop(this);
            Value r = (v.type == V_INT) ? val_make_int(-v.i)
                                        : val_make_fx((fx::fix)(-(fx::fix)val_as_fx(v)));
            val_drop(v);
            push(this, r);
            break;
        }
        case BC_LOAD_NAME: {
            int i = rd_i32(cur, fr->ip); fr->ip += 4;
            Value* p = env_lookup(fr->env, cur->names[i]);
            if (!p) { vm_fail(this, "undefined variable"); }
            else push(this, val_dup(*p));
            break;
        }
        case BC_DEF_NAME: {
            int i = rd_i32(cur, fr->ip); fr->ip += 4;
            Value v = pop(this);
            env_define(fr->env, cur->names[i], v);   // 所有权转入
            break;
        }
        case BC_STORE_NAME: {
            int i = rd_i32(cur, fr->ip); fr->ip += 4;
            Value v = pop(this);
            if (!env_assign(fr->env, cur->names[i], v)) vm_fail(this, "assign to undefined");
            push(this, val_dup(v));
            val_drop(v);
            break;
        }
        case BC_JMP: {
            int t = rd_i32(cur, fr->ip); fr->ip += 4;
            fr->ip = t;
            break;
        }
        case BC_JZ: {
            int t = rd_i32(cur, fr->ip); fr->ip += 4;
            Value v = pop(this);
            bool f = !val_is_truthy(v);
            val_drop(v);
            if (f) fr->ip = t;
            break;
        }
        case BC_POP: {
            Value v = pop(this);
            val_drop(v);
            break;
        }

        case BC_ARRAY: {
            int n = rd_i32(cur, fr->ip); fr->ip += 4;
            Value arr = val_make_arr(n > 0 ? n : 1);
            ArrObj* a = val_as_arr(arr);
            // 栈底在前：先弹出的是最后一个元素
            for (int i = n - 1; i >= 0; i--) {
                Value el = pop(this);
                a->items[i] = el;   // 转移所有权
            }
            a->len = n;
            push(this, arr);
            break;
        }
        case BC_INDEX: {
            Value idx = pop(this);
            Value obj = pop(this);
            ArrObj* arr = val_as_arr(obj);
            Value r;
            if (!arr) { vm_fail(this, "index on non-array"); r = val_make_null(); }
            else r = arr_get(arr, (int)val_as_int(idx));
            val_drop(obj); val_drop(idx);
            push(this, r);
            break;
        }
        case BC_SET_INDEX: {
            Value val = pop(this);
            Value obj = pop(this);
            Value idx = pop(this);
            ArrObj* arr = val_as_arr(obj);
            if (!arr || !arr_set(arr, (int)val_as_int(idx), val))
                vm_fail(this, "index set failed");
            val_drop(obj); val_drop(idx); val_drop(val);
            push(this, val_make_null());
            break;
        }
        case BC_CLOSURE: {
            int pidx = rd_i32(cur, fr->ip); fr->ip += 4;
            const FuncProto* fp = &cur->funcs[pidx];
            Value clo = val_make_closure(fp->fn, 0, fr->env);
            push(this, clo);
            break;
        }
        case BC_CALL: {
            int argc = rd_i32(cur, fr->ip); fr->ip += 4;
            // 弹出实参
            Value* argv = new Value[argc > 0 ? argc : 1];
            for (int i = argc - 1; i >= 0; i--) argv[i] = pop(this);
            Value callee = pop(this);

            BuiltinObj* bi = val_as_builtin(callee);
            ClosureObj* clo = val_as_closure(callee);
            if (bi) {
                Value r = bi->fn(argc, argv, 0);
                for (int i = 0; i < argc; i++) val_drop(argv[i]);
                delete[] argv;
                val_drop(callee);
                push(this, r);
            } else if (clo) {
                // 找到函数入口
                int entry = -1;
                for (int i = 0; i < cur->funcs.size(); i++)
                    if (cur->funcs[i].fn == clo->fn) { entry = cur->funcs[i].entry; break; }
                if (entry < 0) { vm_fail(this, "undefined function"); }
                else {
                    Env* nenv = env_new(clo->env);
                    FuncNode* fn = clo->fn;
                    int n = fn->params.size();
                    if (n > argc) n = argc;
                    for (int i = 0; i < n; i++)
                        env_define(nenv, fn->params[i], argv[i]); // 所有权转入
                    VmFrame nf;
                    nf.chunk = const_cast<CodeChunk*>(cur);
                    nf.ip = entry;
                    nf.env = nenv;
                    frames.push(nf);
                }
                for (int i = 0; i < argc; i++) val_drop(argv[i]);
                delete[] argv;
                val_drop(callee);
            } else {
                vm_fail(this, "call non-function");
                for (int i = 0; i < argc; i++) val_drop(argv[i]);
                delete[] argv;
                val_drop(callee);
            }
            break;
        }
        case BC_RET: {
            Value rv = pop(this);
            // 弹出当前帧，释放其环境（全局帧除外）
            VmFrame done = frames[frames.size() - 1];
            frames.pop();
            if (frames.size() > 0) {
                env_release(done.env);
                push(this, rv);   // 返回值交给调用者
            } else {
                // 主帧结束：rv 作为结果
                push(this, rv);
            }
            break;
        }
        default:
            vm_fail(this, "bad opcode");
            break;
        }
    }

    Value result = val_make_null();
    if (stack.size() > 0) {
        result = stack[stack.size() - 1];
        stack.pop();
    }
    return result;
}

// 便捷入口：解析 + 编译 + 运行
Value minilang_run_vm(const char* source, Env* global_env, String* errmsg_out) {
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

    CodeChunk cc;
    String cerr;
    bool ok = minilang_compile(prog, &cc, &cerr);
    Value r;
    if (!ok) {
        if (errmsg_out) *errmsg_out = cerr;
        r = val_make_null();
    } else {
        Vm vm(g);
        r = vm.run(&cc);
        if (vm.error) {
            if (errmsg_out) *errmsg_out = vm.errmsg;
            val_drop(r);
            r = val_make_null();
        }
    }
    // 释放 chunk 常量
    for (int i = 0; i < cc.consts.size(); i++) val_drop(cc.consts[i]);

    val_drop(progv);
    if (created) env_release(g);
    return r;
}

// 自测试：用 VM 跑若干程序
static int vm_check(const char* src, int64_t expect) {
    String err;
    Value r = minilang_run_vm(src, 0, &err);
    if (val_is_null(r) && expect != -9999) return 1;
    int64_t got = val_as_int(r);
    val_drop(r);
    return (got == expect) ? 0 : 1;
}

int vm_self_test() {
    int fails = 0;
    fails += vm_check("1+2*3", 7);
    fails += vm_check("let x=10; x+5;", 15);
    fails += vm_check("let i=0; let s=0; while(i<5){s=s+i;i=i+1;} s;", 10);
    fails += vm_check("fn fib(n){ if(n<2){return n;} return fib(n-1)+fib(n-2);} fib(10);", 55);
    fails += vm_check("let a=[10,20,30]; a[1];", 20);
    fails += vm_check("fn add(a,b){return a+b;} add(3,4);", 7);
    fails += vm_check("let s=0; for(let i=0;i<4;i=i+1){s=s+i;} s;", 6);
    return fails;
}

} // namespace minilang
} // namespace nefu



