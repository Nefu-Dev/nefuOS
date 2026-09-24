// minilang 宿主测试主程序：跑全部模块 self_test，并做端到端集成测试。
// 编译：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 -I core ^
//     tests/minilang_test_main.cpp core/minilang/*.cpp ^
//     core/klib/memory.cpp core/klib/string.cpp core/klib/printf.cpp ^
//     core/lib/softmath.cpp -o out.exe
#include "../core/minilang/minilang_all.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nefu {
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char* s) { fputs(s, stderr); }
} // namespace nefu

using namespace nefu;
using namespace nefu::minilang;

static int g_fails = 0;

// 用树遍历解释器跑一段代码，断言结果整数等于 expect
static void check_eval(const char* name, const char* src, int64_t expect) {
    String err;
    Value r = minilang_run(src, 0, &err);
    if (val_is_null(r)) {
        printf("  [FAIL] %-14s eval error: %s\n", name, err.c_str());
        g_fails++;
        return;
    }
    int64_t got = val_as_int(r);
    val_drop(r);
    if (got != expect) {
        printf("  [FAIL] %-14s got=%lld expect=%lld\n", name, (long long)got, (long long)expect);
        g_fails++;
    } else {
        printf("  [ ok ] %-14s = %lld\n", name, (long long)got);
    }
}

// 用栈式虚拟机跑同一段代码
static void check_vm(const char* name, const char* src, int64_t expect) {
    String err;
    Value r = minilang_run_vm(src, 0, &err);
    if (val_is_null(r)) {
        printf("  [FAIL] %-14s vm error: %s\n", name, err.c_str());
        g_fails++;
        return;
    }
    int64_t got = val_as_int(r);
    val_drop(r);
    if (got != expect) {
        printf("  [FAIL] %-14s vm got=%lld expect=%lld\n", name, (long long)got, (long long)expect);
        g_fails++;
    }
}

int main() {
    printf("== minilang module self tests ==\n");
    int f = minilang_self_test();
    printf("  module self-test total failures = %d\n", f);
    g_fails += f;

    printf("== end-to-end via tree-walk interpreter ==\n");
    check_eval("arith",     "1+2*3-4/2;", 5);
    check_eval("vars",      "let x=10; let y=5; x*y+1;", 51);
    check_eval("assign",    "let x=1; x=x*10+5; x;", 15);
    check_eval("ifelse",    "let a=0; if(3>2){a=1;}else{a=2;} a;", 1);
    check_eval("while",     "let i=0; let s=0; while(i<10){s=s+i;i=i+1;} s;", 45);
    check_eval("for",       "let s=0; for(let i=1;i<=5;i=i+1){s=s+i;} s;", 15);
    check_eval("array",     "let a=[1,2,3,4,5]; a[0]+a[4];", 6);
    check_eval("arrset",    "let a=[1,2,3]; a[2]=99; a[2];", 99);
    check_eval("func",      "fn add(a,b){return a+b;} add(30,12);", 42);
    check_eval("recursion", "fn fib(n){if(n<2){return n;} return fib(n-1)+fib(n-2);} fib(15);", 610);
    check_eval("closure",   "fn mk(){let c=0; fn inc(){c=c+1; return c;} return inc;} let f=mk(); f(); f(); f();", 3);
    check_eval("builtin",   "sum([10,20,30])+len([1,2]);", 62);
    check_eval("range",     "range(3)[2];", 2);
    check_eval("pushpop",   "let a=[1,2]; push(a,3); pop(a); len(a);", 2);
    check_eval("chrord",    "ord(chr(65));", 65);
    check_eval("reverse",   "reverse([1,2,3])[0];", 3);
    check_eval("hexlit",    "0x10 + 0x0F;", 31);
    check_eval("sqrt",      "int(sqrt(16));", 4);
    check_eval("upper",     "len(upper(\"abc\"));", 3);

    printf("== end-to-end via stack VM ==\n");
    check_vm("vm arith",    "1+2*3;", 7);
    check_vm("vm fib",      "fn fib(n){if(n<2){return n;} return fib(n-1)+fib(n-2);} fib(12);", 144);
    check_vm("vm while",    "let i=1; let p=1; while(i<=5){p=p*i;i=i+1;} p;", 120);
    check_vm("vm array",    "let a=[10,20,30]; a[1];", 20);
    check_vm("vm closure",  "fn mk(x){fn g(){return x*2;} return g;} let f=mk(21); f();", 42);

    printf("== minilang result: %s ==\n", g_fails == 0 ? "ALL PASSED" : "FAILURES");
    return g_fails ? 1 : 0;
}


