// quick host test for minijs (+ klib stubs)
#include "minijs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace nefu {
void platform_dbg(const char* s) { fputs(s, stderr); }
void* kalloc(unsigned long long n) { return malloc((size_t)n); }
void kfree(void* p) { free(p); }
}

int main() {
    struct { const char* name; const char* code; const char* expect; } tests[] = {
        {"arith", "print(1+2*3);", "7\n"},
        {"vars", "var x=5; var y=2; print(x+y);", "7\n"},
        {"ifelse", "var x=10; if (x>5) { print(\"big\"); } else { print(\"small\"); }", "big\n"},
        {"for", "var sum=0; for (var i=1;i<=10;i=i+1) { sum=sum+i; } print(sum);", "55\n"},
        {"while", "var n=3; var c=0; while (n>0) { c=c+1; n=n-1; } print(c);", "3\n"},
        {"strcat", "document.write(\"sum=\" + (1+2));", "sum=3"},
        {"docwrite", "document.write(\"hello\");", "hello"},
        {"len", "print(len(\"abcd\"));", "4\n"},
        {"logic", "var a=1; var b=0; if (a && !b) { print(\"T\"); }", "T\n"},
        {"nested", "var r=0; for (var i=0;i<3;i=i+1) { for (var j=0;j<4;j=j+1) { r=r+1; } } print(r);", "12\n"},
    };
    int pass = 0, fail = 0;
    for (auto& t : tests) {
        char out[512];
        int rc = nefu::mini_js_run(t.code, out, sizeof(out));
        bool ok = (rc == 0) && strcmp(out, t.expect) == 0;
        if (ok) { pass++; }
        else { fail++; printf("FAIL %-8s rc=%d got=[%s] want=[%s]\n", t.name, rc, out, t.expect); }
    }
    printf("pass=%d fail=%d\n", pass, fail);
    return fail ? 1 : 0;
}
