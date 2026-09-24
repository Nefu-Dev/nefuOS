// compiler 宿主测试主程序：跑全部模块 self_test，并做端到端流水线集成测试。
// 编译：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 -I core ^
//     tests/compiler_test_main.cpp core/compiler/lexer.cpp core/compiler/ast.cpp ^
//     core/compiler/parser.cpp core/compiler/ir.cpp core/compiler/codegen.cpp ^
//     core/compiler/assembler.cpp core/compiler/linker.cpp core/compiler/preprocessor.cpp ^
//     core/klib/memory.cpp core/klib/string.cpp core/klib/printf.cpp -o out.exe
#include "../core/compiler/compiler_all.h"
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
using namespace nefu::compiler;

static int g_fails = 0;

// 端到端：跑一条 C 子集源码，断言流水线不报错且产物含关键阶段标记
static void check_pipeline(const char* name, const char* src) {
    String out;
    int errs = compiler_pipeline(src, out);
    bool ok = errs == 0 &&
              out.find("词法") >= 0 &&
              out.find("AST") >= 0 &&
              out.find("IR") >= 0 &&
              out.find("汇编") >= 0;
    if (!ok) {
        printf("  [FAIL] %-14s errs=%d\n", name, errs);
        g_fails++;
    } else {
        printf("  [ ok ] %-14s errs=%d\n", name, errs);
    }
}

int main() {
    printf("== compiler module self tests ==\n");
    int f = compiler_self_test();
    printf("  module self-test total failures = %d\n", f);
    g_fails += f;

    printf("== end-to-end compiler pipeline ==\n");
    check_pipeline("arith",
        "int main(void){ int a = 1 + 2 * 3; return a; }");
    check_pipeline("while",
        "int main(void){ int i = 0; int s = 0; while (i < 5) { s = s + i; i = i + 1; } return s; }");
    check_pipeline("ifelse",
        "int f(int x){ if (x > 0) { return 1; } else { return 0; } } int main(void){ return f(3); }");
    check_pipeline("funcmacro",
        "#define SQR(x) ((x)*(x))\nint main(void){ int a = SQR(4); return a; }");
    check_pipeline("withmacro",
        "#define N 7\nint main(void){ int x = N; return x; }");
    check_pipeline("dowhile",
        "int main(void){ int i = 0; int s = 0; do { s = s + i; i = i + 1; } while (i < 4); return s; }");
    check_pipeline("forloop",
        "int main(void){ int s = 0; for (int i = 0; i < 5; i = i + 1) { s = s + i; } return s; }");
    check_pipeline("call",
        "int add(int a, int b){ return a + b; } int main(void){ int x = add(2, 3); return x; }");

    check_pipeline("nestedif",
        "int f(int x){ if (x > 0) { if (x > 10) { return 1; } return 2; } return 3; } int main(void){ return f(7); }");
    check_pipeline("global",
        "int g = 100; int main(void){ return g + 1; }");
    check_pipeline("breakloop",
        "int main(void){ int i = 0; int s = 0; while (i < 100) { if (i >= 5) { break; } s = s + i; i = i + 1; } return s; }");
    check_pipeline("divmod",
        "int main(void){ int a = 20; int b = 6; int q = a / b; int r = a % b; return q * 10 + r; }");
    check_pipeline("submul",
        "int main(void){ int a = 10; int b = 4; int c = a - b; int d = c * 3; return d; }");
    check_pipeline("add3",
        "int add3(int a, int b, int c){ return a + b + c; } int main(void){ return add3(1, 2, 3); }");
    check_pipeline("dosum",
        "int main(void){ int i = 1; int s = 0; do { s = s + i; i = i + 1; } while (i <= 4); return s; }");
    check_pipeline("forsum",
        "int main(void){ int s = 0; for (int i = 1; i <= 4; i = i + 1) { s = s + i; } return s; }");
    check_pipeline("whilesum",
        "int main(void){ int i = 1; int s = 0; while (i <= 4) { s = s + i; i = i + 1; } return s; }");
    check_pipeline("call2",
        "int doubleit(int x){ return x * 2; } int main(void){ return doubleit(21); }");
    check_pipeline("locals",
        "int main(void){ int a = 1; int b = 2; int c = 3; int d = a + b + c; return d; }");
    check_pipeline("earlyret",
        "int sign(int x){ if (x < 0) { return -1; } if (x == 0) { return 0; } return 1; } int main(void){ return sign(-5) + sign(0) + sign(9); }");
    check_pipeline("dowhbrk",
        "int main(void){ int i = 0; int s = 0; do { if (i >= 3) { break; } s = s + i; i = i + 1; } while (i < 10); return s; }");
    check_pipeline("forbrk",
        "int main(void){ int s = 0; for (int i = 0; i < 10; i = i + 1) { if (i == 3) { break; } s = s + i; } return s; }");
    check_pipeline("whilebrk",
        "int main(void){ int i = 0; int s = 0; while (1) { if (i >= 4) { break; } s = s + i; i = i + 1; } return s; }");
    check_pipeline("globa2",
        "int g1 = 1; int g2 = 2; int main(void){ return g1 + g2; }");
    check_pipeline("elseif",
        "int main(void){ int a = 2; int s = 0; if (a == 1) { s = 10; } else { if (a == 2) { s = 20; } else { s = 30; } } return s; }");
    check_pipeline("assign",
        "int main(void){ int x = 1; int y = 2; x = x + y; x = x * y; return x; }");
    check_pipeline("logic",
        "int main(void){ int a = 1; int b = 0; int c = a && b; int d = a || b; return c + d; }");
    check_pipeline("callchain",
        "int add2(int x, int y){ return x + y; } int main(void){ int a = add2(3, 4); int b = add2(a, 5); return b; }");
    check_pipeline("compare",
        "int main(void){ int a = 5; int b = 8; int s = 0; if (a < b) { s = 1; } else { s = 0; } if (b <= a) { s = 2; } return s; }");
    check_pipeline("bitwise",
        "int main(void){ int a = 12; int b = 10; int c = a & b; int d = a | b; return c + d; }");
    check_pipeline("charlit",
        "int main(void){ char c = 65; return c; }");
    check_pipeline("unary",
        "int main(void){ int a = 5; int b = -a; int c = !b; return c; }");
    check_pipeline("nestedloop",
        "int main(void){ int s = 0; for (int i = 0; i < 3; i = i + 1) { for (int j = 0; j < 3; j = j + 1) { s = s + 1; } } return s; }");
    check_pipeline("retvoid",
        "void f(void){ return; } int main(void){ f(); return 0; }");
    check_pipeline("modarith",
        "int main(void){ int a = 17; int b = 5; int c = a % b; return c; }");
    check_pipeline("manyargs",
        "int sum3(int a, int b, int c){ return a + b + c; } int main(void){ return sum3(1,2,3); }");
    check_pipeline("arithmetic",
        "int main(void){ int a = 10; int b = 3; int c = a / b; int d = a % b; return c + d; }");

    // 负面用例：引用未声明变量，语义分析应报错
    {
        String bad;
        int e = compiler_pipeline("int main(void){ return not_a_var; }", bad);
        if (e == 0 && bad.find("未声明") < 0) { printf("  [FAIL] undeclared\n"); g_fails++; }
        else printf("  [ ok ] undeclared\n");
    }

    // 统计接口：应返回 0 错误且产物含计数
    {
        String s;
        int e = compiler_stats("int main(void){ int a = 1; return a; }", s);
        if (e != 0 || s.find("tokens=") < 0 || s.find("functions=1") < 0) {
            printf("  [FAIL] stats\n"); g_fails++;
        } else printf("  [ ok ] stats\n");
    }

    printf("== compiler result: %s ==\n", g_fails == 0 ? "ALL PASSED" : "FAILURES");
    return g_fails ? 1 : 0;
}
