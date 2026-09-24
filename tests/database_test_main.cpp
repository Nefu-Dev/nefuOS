// nefuOS 嵌入式数据库 —— 独立宿主测试 main
//
// 用法(在 nefuOS 根目录下):
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -O2 ^
//     -I core tests\database_test_main.cpp ^
//     core\database\*.cpp core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\database_test.exe
//
// 本文件自带 kalloc/kfree/krealloc/platform_dbg 的宿主桩,
// 不依赖任何 OS / VFS / GUI 代码,因此可单独编译跑全部 self_test。

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ---- 宿主内存桩:kalloc/kfree 直接转发到 libc malloc ----
namespace nefu {
void* kalloc(size_t sz) {
    void* p = malloc(sz ? sz : 1);
    return p;
}
void kfree(void* p) {
    free(p);
}
void* krealloc(void* p, size_t sz) {
    return realloc(p, sz ? sz : 1);
}
} // namespace nefu

// ---- 调试输出桩:打印到 stdout ----
namespace nefu {
void platform_dbg(const char* s) {
    fputs(s, stdout);
    fputc('\n', stdout);
}
} // namespace nefu

// ---- 被测库 ----
#include "database/database_all.h"

using namespace nefu;
using namespace nefu::database;

int main() {
    int w   = wal_self_test();
    int b   = btree_self_test();
    int kv  = kvstore_self_test();
    int sp  = sqlparse_self_test();
    int se  = sqlexec_self_test();
    int cu  = cursor_self_test();
    int all = database_self_test();

    printf("wal self_test      = %d failures\n", w);
    printf("btree self_test    = %d failures\n", b);
    printf("kvstore self_test  = %d failures\n", kv);
    printf("sqlparse self_test = %d failures\n", sp);
    printf("sqlexec self_test  = %d failures\n", se);
    printf("cursor self_test   = %d failures\n", cu);
    printf("--------------------------------\n");
    printf("TOTAL              = %d failures\n", all);

    // 额外做一轮端到端 SQL 冒烟,确认解析+执行串起来
    Database db;
    db.exec("CREATE TABLE emp (id, name, dept, salary)");
    db.exec("INSERT INTO emp VALUES (1, 'Alice', 'Eng', 9000)");
    db.exec("INSERT INTO emp VALUES (2, 'Bob', 'Sales', 7000)");
    db.exec("INSERT INTO emp VALUES (3, 'Carol', 'Eng', 12000)");
    db.exec("INSERT INTO emp VALUES (4, 'Dan', 'Eng', 8000)");
    SqlResult r = db.exec("SELECT name, salary FROM emp WHERE dept = 'Eng' ORDER BY salary");
    printf("smoke: Eng employees = %d rows\n", r.rows.size());
    for (int i = 0; i < r.rows.size(); i++) {
        printf("  %s salary=%s\n",
               r.rows[i].cells[0].c_str(), r.rows[i].cells[1].c_str());
    }

    if (all == 0 && r.rows.size() == 3) {
        printf("\nALL DATABASE TESTS PASSED\n");
        return 0;
    }
    printf("\nDATABASE TESTS FAILED\n");
    return 1;
}
