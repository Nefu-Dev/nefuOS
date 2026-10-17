// dbg1.cpp — run a single .ncode script from a UTF-8 file, print PASS/FAIL + output
#include "apps/ncode.h"
#include "vfs/vfs.h"
#include "klib/klib.h"
#include "sys/sysapi.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace nefu {
uint32_t platform_tick_ms() { static uint32_t t = 1000000; return t++; }
void    platform_thread_sleep(uint32_t ms) { (void)ms; }
void    platform_dbg(const char* s) { fputs(s ? s : "", stderr); }
void*   kalloc(size_t n) { return malloc(n ? n : 1); }
void    kfree(void* p) { free(p); }
int     nefud_name_to_app_id(const char* n) { (void)n; return -1; }
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

struct TOut { nefu::String buf;
  static void cb(void* ud, const char* s) { fputs(s, stdout); fflush(stdout); ((TOut*)ud)->buf += s; }
  nefu::NCOut out() { nefu::NCOut o; o.print=&TOut::cb; o.ud=this; return o; } };

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { printf("usage: dbg1 <script.txt>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc(n + 1);
    size_t rd = fread(src, 1, (size_t)n, f); src[rd] = 0; fclose(f);

    nefu::g_vfs = new nefu::VFS();
    nefu::g_vfs->mkdir("/home"); nefu::g_vfs->mkdir("/home/user");
    nefu::g_vfs->mkdir("/home/user/ncode"); nefu::g_vfs->mkdir("/home/user/ncode/api");

    TOut to; nefu::NCOut sink = to.out();
    printf("running %s (%ld bytes)...\n", argv[1], (long)rd);
    bool ok = nefu::ncode_run_source(&sink, src, (int)rd, argv[1], 0, 0);
    printf("RESULT ok=%d\n", ok ? 1 : 0);
    printf("---- output ----\n%s\n---- end ----\n", to.buf.c_str());
    return ok ? 0 : 1;
}
