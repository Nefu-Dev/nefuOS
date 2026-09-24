// nefuOS 文本处理库独立编译测试
// 编译：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -O2 -I core ^
//     tests\textproc_test_main.cpp core\textproc\*.cpp core\klib\memory.cpp ^
//     core\klib\string.cpp core\klib\printf.cpp -o %TEMP%\textproc_test.exe
#include "textproc/textproc_all.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 宿主桩：klib/memory.cpp 的 operator new 路由到 kalloc，printf.cpp 依赖 platform_dbg。
// 裸机/宿主正式构建由平台层提供；这里用标准库补齐，便于独立编译验证。
namespace nefu {
void* kalloc(size_t n) { return std::malloc(n ? n : 1); }
void  kfree(void* p) { std::free(p); }
void  platform_dbg(const char*) {}
}

using namespace nefu::textproc;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int total = 0, f;

    f = editdist_self_test();   printf("editdist_self_test : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = search_self_test();     printf("search_self_test    : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = trie_self_test();       printf("trie_self_test      : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = stats_self_test();      printf("stats_self_test     : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = phonetic_self_test();   printf("phonetic_self_test  : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = tokenize_self_test();   printf("tokenize_self_test  : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;

    // 冒烟：抽查几个公开 API 的已知值
    if (levenshtein("kitten","sitting") != 3) { printf("  !! levenshtein kitten/sitting\n"); total++; }
    if (kmp_first("ABABC","ABABABC") != 2)      { printf("  !! kmp index\n"); total++; }
    char se[5]; soundex("Robert", se);
    if (strcmp(se,"R163") != 0) { printf("  !! soundex Robert=%s\n", se); total++; }

    int all = textproc_self_test();
    printf("textproc_self_test (aggregated): %s (%d)\n", all==0?"PASS":"FAIL", all);

    printf("\n=== RESULT: %s (total failures=%d) ===\n", total==0?"ALL PASS":"SOME FAILURES", total);
    return total==0 ? 0 : 1;
}
