// nefuOS 系统工具扩展库 —— 独立宿主测试主程序
// 编译：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 ^
//     -I core tests\sysutil_test_main.cpp core\sysutil\*.cpp ^
//     core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\sysutil_test.exe
// 这里只提供 kalloc/kfree/krealloc/platform_* 桩，然后跑全部 self_test。
#include "../core/sysutil/sysutil_all.h"
#include "../core/platform.h"
#include <cstdio>
#include <cstdlib>

namespace nefu {
// 宿主桩：裸机里 kalloc/kfree 由后端提供，宿主测试用 malloc/free。
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char* s) { fputs(s, stderr); }

// sysinfo 用到的平台桩：返回一组确定值，self_test 走 mock 路径，这里仅为链接。
bool platform_hw_info(HwInfo* out) {
    if (!out) return false;
    const char* m = "Nefu-Test-CPU";
    for (int i = 0; i < 63 && m[i]; i++) out->cpu_model[i] = m[i];
    out->cpu_model[63] = 0;
    out->cpu_mhz = 2400;
    out->mem_total_mb = 4096;
    out->bios_version[0] = 0;
    out->bios_vendor[0] = 0;
    out->cpu_cores = 2;
    return true;
}
void platform_mem_stats(uint32_t* used, uint32_t* total) {
    if (used) *used = 1024;
    if (total) *total = 4096;
}
uint32_t nefuos_uptime_ms() { return 0; }

} // namespace nefu

using namespace nefu::sysutil;

// sysutil 独立测试入口：桩平台函数 + 调用全部 self_test。
int main() {
    // 分模块跑，便于定位
    int fp = g_procman.self_test();
    int fi = sysinfo_self_test();
    int fe = g_syslog.self_test();
    int fs = g_scheduler.self_test();
    int fpe = g_perms.self_test();
    int fsv = g_svcmgr.self_test();
    int fc = g_config.self_test();
    int total = fp + fi + fe + fs + fpe + fsv + fc;

    printf("sysutil self test:\n");
    printf("  process    failures = %d\n", fp);
    printf("  sysinfo    failures = %d\n", fi);
    printf("  eventlog   failures = %d\n", fe);
    printf("  scheduler  failures = %d\n", fs);
    printf("  permissions failures = %d\n", fpe);
    printf("  services   failures = %d\n", fsv);
    printf("  config     failures = %d\n", fc);
    printf("  TOTAL      failures = %d\n", total);

    // ---- 额外场景：题目要求的端到端冒烟 ----
    // 1) 进程创建/终止
    {
        g_procman.set_now(0);
        int pid = proc_create("smoke", PRIO_NORMAL, 4, 0);
        if (pid <= 0) { printf("  SMOKE proc create FAIL\n"); total++; }
        for (int i = 0; i < 50; i++) proc_advance(10);
        if (g_procman.count() != 0) { printf("  SMOKE proc reap FAIL\n"); total++; }
    }
    // 2) 日志写入/查询
    {
        g_syslog.clear();
        g_syslog.set_now(0);
        log_write(LOG_WARN, "smoke", "disk %d%% full", 75);
        LogFilter f; f.min_level = LOG_WARN; f.src_prefix[0] = 0;
        LogEntry e[4];
        int n = log_query(&f, e, 4);
        if (n != 1) { printf("  SMOKE log query FAIL n=%d\n", n); total++; }
    }
    // 3) 调度器触发
    {
        Scheduler s;
        int id = s.add_periodic("tick", 50);
        FiredTask ev[4];
        int n = s.tick(60, ev, 4);
        if (n != 1) { printf("  SMOKE sched trigger FAIL n=%d\n", n); total++; }
        (void)id;
    }
    // 4) 权限检查
    {
        PermissionManager pm; pm.reset();
        int u = pm.add_user("u1", 100);
        // 文件 644 owned by root: other 可读
        if (!pm.check(u, 0, 0, 0644, ACCESS_READ)) { printf("  SMOKE perm read FAIL\n"); total++; }
        if (pm.check(u, 0, 0, 0644, ACCESS_WRITE)) { printf("  SMOKE perm write FAIL\n"); total++; }
    }
    // 5) 配置读写
    {
        ConfigStore c;
        c.set_str("a", "k", "v");
        if (nefu::strcmp(c.get_str("a", "k", "x"), "v") != 0) { printf("  SMOKE cfg FAIL\n"); total++; }
        nefu::String dmp; c.dump(dmp);
        if (dmp.len() < 3) { printf("  SMOKE cfg dump FAIL\n"); total++; }
    }
    // 6) IPC 消息收发
    {
        g_procman.set_now(0);
        int a = g_procman.create_process("sa", PRIO_NORMAL, 50, 0);
        int b = g_procman.create_process("sb", PRIO_NORMAL, 50, 0);
        if (!g_procman.ipc_send(b, "ping", 5)) { printf("  SMOKE ipc send FAIL\n"); total++; }
        char buf[128];
        int r = g_procman.ipc_recv(b, buf);
        if (r != 4 || buf[0] != 'p') { printf("  SMOKE ipc recv FAIL r=%d\n", r); total++; }
    }
    // 7) 服务依赖启动
    {
        ServiceManager sv; sv.reset();
        const char* dep0[] = {0};
        sv.register_service("net", dep0, true);
        const char* dep1[] = {"net", 0};
        sv.register_service("web", dep1, true);
        sv.start("web");
        if (sv.state_of("web") != SVC_RUNNING) { printf("  SMOKE svc start FAIL\n"); total++; }
    }
    // 8) 日志序列化往返
    {
        EventLog le; le.clear(); le.set_now(0);
        le.write(LOG_INFO, "t", "hello");
        uint8_t blob[33000];
        int n = le.serialize(blob, sizeof(blob));
        if (n <= 0) { printf("  SMOKE log ser FAIL\n"); total++; }
        EventLog le2;
        int m = le2.deserialize(blob, n);
        if (m <= 0 || le2.count() != 1) { printf("  SMOKE log deser FAIL\n"); total++; }
    }
    // 9) 权限组与文件
    {
        PermissionManager pm; pm.reset();
        int u = pm.add_user("smoke", 100);
        pm.fs_register("/etc/hostname", 0, 0, 0644);
        if (pm.fs_mode("/etc/hostname") != 0644) { printf("  SMOKE fs mode FAIL\n"); total++; }
        if (!pm.in_group(0, 0)) { printf("  SMOKE root group FAIL\n"); total++; }
    }
    // 10) 配置点路径
    {
        ConfigStore c;
        c.set_int("web", "port", 8080);
        if (c.get_int_dotted("web.port", 0) != 8080) { printf("  SMOKE cfg dotted FAIL\n"); total++; }
        if (!c.has_key("web", "port")) { printf("  SMOKE cfg haskey FAIL\n"); total++; }
    }

    // 10a) config get_bool
    {
        ConfigStore cb;
        cb.set_bool("web", "https", true);
        if (!cb.get_bool("web", "https", false)) { printf("  SMOKE cfg bool FAIL\n"); total++; }
    }
    // 10b) config to_json
    {
        ConfigStore cj;
        cj.set_int("a", "b", 1);
        nefu::String out;
        cj.to_json(out);
        if (out.len() < 5) { printf("  SMOKE cfg json FAIL\n"); total++; }
    }
    // 10b2) config keys_of
    {
        ConfigStore ck;
        ck.set_int("s", "k1", 1);
        ck.set_int("s", "k2", 2);
        const char* keys[4];
        int kn = ck.keys_of("s", keys, 4);
        if (kn < 2) { printf("  SMOKE cfg keys FAIL n=%d\n", kn); total++; }
    }
    // 10c) config sections
    {
        ConfigStore cs;
        cs.set_int("sec1", "k", 1);
        cs.set_int("sec2", "k", 2);
        const char* names[4];
        int n = cs.sections(names, 4);
        if (n < 2) { printf("  SMOKE cfg sections FAIL n=%d\n", n); total++; }
    }
    // 11a) eventlog 级别过滤
    {
        EventLog lx2; lx2.clear(); lx2.set_now(0);
        lx2.set_min_level(LOG_WARN);
        lx2.write(LOG_INFO, "x", "should drop");
        if (lx2.count() != 0) { printf("  SMOKE log minlevel FAIL\n"); total++; }
    }
    // 11b) 权限 umask
    {
        PermissionManager pm2; pm2.reset();
        pm2.set_umask(0022);
        if (pm2.get_umask() != 0022) { printf("  SMOKE umask FAIL\n"); total++; }
    }
    // 11) 调度器 max_runs
    {
        Scheduler s;
        int id = s.add_periodic("once", 10);
        s.set_max_runs(id, 1);
        FiredTask ev[4];
        s.tick(0, ev, 4);
        int n = s.tick(10, ev, 4);
        if (n != 1) { printf("  SMOKE sched maxruns FAIL n=%d\n", n); total++; }
        if (s.tick(20, ev, 4) != 0) { printf("  SMOKE sched maxruns2 FAIL\n"); total++; }
    }

    if (total == 0) printf("ALL SYSUTIL TESTS PASSED\n");
    else printf("FAILURES DETECTED (%d)\n", total);
    return total ? 1 : 0;
}
// 测试结束
