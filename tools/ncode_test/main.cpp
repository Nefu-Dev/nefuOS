// =============================================================================
//  ncode_test/main.cpp — host-side unit harness for the .ncode interpreter
// -----------------------------------------------------------------------------
//  Build (from repo root):
//    g++ -std=c++17 -O0 -fno-exceptions -fno-rtti -fno-builtin -DNCODE_HOST_TEST
//        -DNEFU_DUMP_REC -I core -I third_party
//        tools\ncode_test\main.cpp
//        core\apps\ncode.cpp core\klib\string.cpp core\klib\memory.cpp
//        core\klib\printf.cpp core\vfs\vfs.cpp
//        -o ncode_test.exe
//  Run: ncode_test.exe   (exit code 0 = all checks passed)
// =============================================================================
#include "apps/ncode.h"
#include "vfs/vfs.h"
#include "klib/klib.h"
#include "sys/sysapi.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- host platform stubs (standalone; the real ones live in backends) ----
namespace nefu {
uint32_t platform_tick_ms() { static uint32_t t = 1000000; return t++; }
void    platform_thread_sleep(uint32_t ms) { (void)ms; }
void    platform_dbg(const char* s) { fputs(s ? s : "", stderr); }
void*   kalloc(size_t n) { return malloc(n ? n : 1); }
void    kfree(void* p) { free(p); }
int     nefud_name_to_app_id(const char* n) { (void)n; return -1; }   // OS binding lookup stub
}

namespace nefu { namespace sysapi {
int process_pid() { return 4242; }
bool http_get(const char* url, uint8_t** r, uint32_t* l) { (void)url; *r = 0; *l = 0; return false; }
bool network_available() { return false; }
int screen_width() { return 800; }
int screen_height() { return 600; }
int screen_bpp() { return 32; }
void power_shutdown() {}
void power_reboot() {}
void power_reboot_to_bios() {}
void power_suspend() {}
const char* settings_get(const char* k) { (void)k; return ""; }
void settings_set(const char* k, const char* v) { (void)k; (void)v; }
void settings_save() {}
uint64_t time_now() { return 1700000000ull; }
uint64_t time_uptime() { return 100ull; }
void notify(const char* t, const char* m) { (void)t; (void)m; }
int notification_count() { return 0; }
void notification_mark_all_read() {}
void notification_clear() {}
bool app_launch(int id) { (void)id; return true; }
int app_list(int* ids, int max) { (void)ids; (void)max; return 0; }
bool app_installed(int id) { (void)id; return true; }
} }

// ---- helpers ----
struct TOut {
    nefu::String buf;
    static void cb(void* ud, const char* s) { ((TOut*)ud)->buf += s; }
    nefu::NCOut out() { nefu::NCOut o; o.print = &TOut::cb; o.ud = this; return o; }
};

static int g_fails = 0;

static void check(bool cond, const char* what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fails++;
}

// run a script; returns (ok, output) pair
static bool run_src(const char* path, const char* src, const char** args, int argc, nefu::String* outbuf) {
    TOut to;
    nefu::NCOut sink = to.out();
    bool ok = nefu::ncode_run_source(&sink, src, (int)nefu::strlen(src), path, args, argc);
    if (outbuf) *outbuf = to.buf;
    return ok;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);   // live output for hang diagnosis
    printf("=== ncode host test ===\n");

    // FS scaffold (mirrors what the OS builds)
    nefu::g_vfs = new nefu::VFS();
    nefu::g_vfs->mkdir("/home");
    nefu::g_vfs->mkdir("/home/user");
    nefu::g_vfs->mkdir("/home/user/ncode");
    nefu::g_vfs->mkdir("/home/user/ncode/api");

    // 1) built-in self test
    int sf = nefu::ncode_self_test();
    printf("self_test failures=%d\n", sf);
    if (sf == 0) printf("PASS interpreter self test\n"); else { printf("FAIL interpreter self test\n"); g_fails++; }

    // 2) basics + strings + arrays
    {
        nefu::String out;
        bool ok = run_src("basics.ncode",
            "#nefu\n"
            "import<std>\n"
            "var g = 10;\n"
            "func add(a, b) { return a + b; }\n"
            "main() {\n"
            "  println(\"g=\" + g);\n"
            "  println(\"sum=\" + add(2, 3));\n"
            "  var s = \"  Hello  \";\n"
            "  println(s.trim().lower());\n"
            "  var a = [1,2,3];\n"
            "  a.push(9);\n"
            "  println(\"arr=\" + a.join(\",\"));\n"
            "  println(a.len);\n"
            "  for (var i = 0; i < a.len; i++) { if (a[i] == 2) println(\"hit2\"); }\n"
            "}\n", 0, 0, &out);
        check(ok, "basics runs");
        if (!ok) printf("  out: %s\n", out.c_str());
        check(out.find("g=10") >= 0, "global var visible");
        check(out.find("sum=5") >= 0, "function call works");
        check(out.find("hello") >= 0, "string trim+lower works");
        check(out.find("arr=1,2,3,9") >= 0, "array push+join works");
        check(out.find("hit2") >= 0, "array indexing in loop");
    }

    // 3) sysapi import + file API through the VFS
    {
        nefu::String out;
        bool ok = run_src("api.ncode",
            "#nefu\n"
            "import<std>\nimport<sysapi>\n"
            "main() {\n"
            "  var fd = file_open(\"/home/user/ncode/api/notes.txt\", 1);\n"
            "  if (fd < 0) { println(\"open-fail\"); return; }\n"
            "  file_write(fd, \"hello-from-ncode\", 16);\n"
            "  file_close(fd);\n"
            "  var all = file_read_all(\"/home/user/ncode/api/notes.txt\");\n"
            "  println(\"read=\" + all);\n"
            "  println(\"pid=\" + process_pid());\n"
            "  println(\"screen=\" + screen_width() + \"x\" + screen_height());\n"
            "}\n", 0, 0, &out);
        check(ok, "sysapi script runs");
        if (!ok) printf("  out: %s\n", out.c_str());
        check(out.find("read=hello-from-ncode") >= 0, "file_write+file_read_all round trip");
        check(out.find("pid=4242") >= 0, "sysapi process_pid reaches interpreter");
        check(out.find("screen=800x600") >= 0, "sysapi display API reaches interpreter");
        // verify the file exists in the VFS
        nefu::FSNode* f = nefu::g_vfs->resolve("/home/user/ncode/api/notes.txt");
        check(f && f->size == 16, "file persisted in VFS (16 bytes)");
        // second run must still see it (persistence)
        nefu::String out2;
        bool ok2 = run_src("api2.ncode",
            "import<std>\nimport<sysapi>\n"
            "main() { println(file_read_all(\"/home/user/ncode/api/notes.txt\")); }\n", 0, 0, &out2);
        check(ok2 && out2.find("hello-from-ncode") >= 0, "file readable across runs");
    }

    // 4) gating: std funcs need import<std>, sysapi funcs need import<sysapi>
    {
        nefu::String out;
        bool ok = run_src("gate.ncode",
            "main() { var q = 5; }\n", 0, 0, &out);
        check(ok, "plain script runs without imports");
        nefu::String out2;
        bool ok2 = run_src("gate2.ncode",
            "import<sysapi>\n"
            "main() { var q = 7; }\n", 0, 0, &out2);
        check(ok2, "import<sysapi> only still runs");
        nefu::String out3;
        bool ok3 = run_src("gate3.ncode",
            "main() { var x = 1; println(x); }\n", 0, 0, &out3);
        // println is a std function -> should be unknown without import<std>
        check(!ok3, "std function gated without import<std>");
        nefu::String out4;
        bool ok4 = run_src("gate4.ncode",
            "main() { var x = process_pid(); println(x); }\n", 0, 0, &out4);
        check(!ok4, "sysapi function gated without import<sysapi>");
    }

    // 5) args()
    {
        const char* args[] = { "alpha", "beta", "gamma" };
        nefu::String out;
        bool ok = run_src("args.ncode",
            "import<std>\n"
            "main() { println(\"n=\" + args().len); println(args()[1]); }\n", args, 3, &out);
        check(ok, "args script runs");
        check(out.find("n=3") >= 0 && out.find("beta") >= 0, "args() returns extra CLI args");
    }

    // 6) start_r_os in host-test mode: must be stubbed, not crash
    {
        nefu::String out;
        bool ok = run_src("os.ncode",
            "import<std>\n"
            "main() { var os = start_r_os(); println(\"after\"); }\n", 0, 0, &out);
        check(ok, "start_r_os host stub does not crash");
        check(out.find("after") >= 0, "script continues after start_r_os()");
        check(out.find("not available") >= 0, "host stub reports not available");
    }

    // 7) missing main -> compile error
    {
        nefu::String out;
        bool ok = run_src("nomain.ncode", "import<std>\nfunc f() { return 1; }\n", 0, 0, &out);
        check(!ok, "missing main() is a compile error");
        check(out.find("main") >= 0, "error mentions main");
    }

    // 8) recursion + floats
    {
        nefu::String out;
        bool ok = run_src("fib.ncode",
            "import<std>\n"
            "func fib(n) { if (n < 2) return n; return fib(n-1) + fib(n-2); }\n"
            "main() { println(\"fib10=\" + fib(10)); println(\"f=\" + (7.5 * 2)); }\n", 0, 0, &out);
        check(ok, "fib runs");
        check(out.find("fib10=55") >= 0, "fib(10)=55");
        check(out.find("f=15") >= 0, "float arithmetic works");
    }

    printf("=== %s (%d failure%s) ===\n", g_fails == 0 ? "ALL PASS" : "FAILED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails == 0 ? 0 : 1;
}
