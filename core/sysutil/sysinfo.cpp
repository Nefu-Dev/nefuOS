// nefuOS 系统工具扩展库 —— 系统信息模块实现
#include "sysinfo.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

namespace {

// 内部缓存：mock 数据；当 mock_set_ 为真时 refresh 直接返回它。
SysInfo g_cache;
bool    g_mock_set = false;

// 手动安全拷贝：dst 最多存 dstsz-1 字节并保证 NUL 结尾。
void copy_str(char* dst, int dstsz, const char* src) {
    if (!dst || dstsz <= 0) return;
    if (!src) src = "";
    int i = 0;
    for (; i < dstsz - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

// 手写 uint64 转串：ksprintf 不支持 %llu。
char* u64_to_str(uint64_t v, char* buf, int bufsz) {
    if (!buf || bufsz < 2) { if (buf && bufsz > 0) buf[0] = 0; return buf; }
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (n > 0 && j < bufsz - 1) buf[j++] = tmp[--n];
    buf[j] = 0;
    return buf;
}

// 环境变量存储
struct EnvSlot {
    bool used;
    char key[ENV_KEY];
    char val[ENV_VAL];
};
EnvSlot g_env[ENV_MAX];
int g_env_used = -1;   // -1 = 尚未初始化

void env_init() {
    if (g_env_used >= 0) return;
    for (int i = 0; i < ENV_MAX; i++) {
        g_env[i].used = false;
        g_env[i].key[0] = 0;
        g_env[i].val[0] = 0;
    }
    g_env_used = 0;
    // 出厂默认环境
    env_set("PATH", "/bin:/usr/bin");
    env_set("HOME", "/root");
    env_set("USER", "root");
    env_set("SHELL", "/bin/nefsh");
}

} // namespace

void sysinfo_refresh(SysInfo* out) {
    if (!out) return;
    if (g_mock_set) { *out = g_cache; return; }

    // 真平台路径
    HwInfo hw;
    bool have_hw = platform_hw_info(&hw);
    if (have_hw) {
        copy_str(out->cpu_model, sizeof(out->cpu_model), hw.cpu_model);
        out->cpu_mhz = hw.cpu_mhz;
        out->cpu_cores = hw.cpu_cores;
        out->mem_total_mb = hw.mem_total_mb;
    } else {
        copy_str(out->cpu_model, sizeof(out->cpu_model), "Unknown CPU");
        out->cpu_mhz = 0;
        out->cpu_cores = 1;
        out->mem_total_mb = 0;
    }
    uint32_t used = 0, total = 0;
    platform_mem_stats(&used, &total);
    out->mem_used_mb = used;
    if (out->mem_total_mb == 0) out->mem_total_mb = total;

    // 磁盘：宿主/裸机暂用平台块设备估算；无信息时给 0
    out->disk_total_mb = 2048;   // 2GB 根分区占位
    out->disk_used_mb = 512;

    out->uptime_ms = nefuos_uptime_ms();
    copy_str(out->os_name, sizeof(out->os_name), "nefuOS");
    copy_str(out->build_tag, sizeof(out->build_tag), "sysutil-1.0");
}

void sysinfo_set_mock(const SysInfo* m) {
    if (!m) { g_mock_set = false; return; }
    g_cache = *m;
    g_mock_set = true;
}

double sysinfo_mem_percent(const SysInfo* s) {
    if (!s || s->mem_total_mb == 0) return 0.0;
    double p = (double)s->mem_used_mb * 100.0 / (double)s->mem_total_mb;
    if (p < 0.0) p = 0.0;
    if (p > 100.0) p = 100.0;
    return p;
}

double sysinfo_disk_percent(const SysInfo* s) {
    if (!s || s->disk_total_mb == 0) return 0.0;
    double p = (double)s->disk_used_mb * 100.0 / (double)s->disk_total_mb;
    if (p < 0.0) p = 0.0;
    if (p > 100.0) p = 100.0;
    return p;
}

uint32_t sysinfo_uptime_sec(const SysInfo* s) {
    return s ? (uint32_t)(s->uptime_ms / 1000u) : 0;
}

void sysinfo_format_uptime(uint32_t total_sec, char* buf, int bufsz) {
    if (!buf || bufsz < 8) { if (buf && bufsz > 0) buf[0] = 0; return; }
    uint32_t d = total_sec / 86400u;
    uint32_t h = (total_sec % 86400u) / 3600u;
    uint32_t m = (total_sec % 3600u) / 60u;
    uint32_t sec = total_sec % 60u;
    // ksprintf 不支持 %02d，手工拼两位
    char hh[4], mm[4], ss[4];
    ksprintf(hh, sizeof(hh), "%d", (int)h);
    ksprintf(mm, sizeof(mm), "%d", (int)m);
    ksprintf(ss, sizeof(ss), "%d", (int)sec);
    // 补前导零
    char hh2[4], mm2[4], ss2[4];
    hh2[0] = (h < 10) ? '0' : hh[0]; hh2[1] = (h < 10) ? hh[0] : hh[1]; hh2[2] = 0;
    mm2[0] = (m < 10) ? '0' : mm[0]; mm2[1] = (m < 10) ? mm[0] : mm[1]; mm2[2] = 0;
    ss2[0] = (sec < 10) ? '0' : ss[0]; ss2[1] = (sec < 10) ? ss[0] : ss[1]; ss2[2] = 0;
    ksprintf(buf, (size_t)bufsz, "%dd %s:%s:%s", (int)d, hh2, mm2, ss2);
}

void sysinfo_format_mb(uint64_t mb, char* buf, int bufsz) {
    if (!buf || bufsz < 4) { if (buf && bufsz > 0) buf[0] = 0; return; }
    char num[24];
    if (mb >= 1024ull * 1024ull) {
        // 以 TB 显示，保留 1 位小数
        uint64_t v = mb / (1024ull * 1024ull);
        uint64_t frac = (mb % (1024ull * 1024ull)) * 10ull / (1024ull * 1024ull);
        u64_to_str(v, num, sizeof(num));
        ksprintf(buf, (size_t)bufsz, "%s.%d TB", num, (int)frac);
    } else if (mb >= 1024) {
        uint64_t v = mb / 1024ull;
        uint64_t frac = (mb % 1024ull) * 10ull / 1024ull;
        u64_to_str(v, num, sizeof(num));
        ksprintf(buf, (size_t)bufsz, "%s.%d GB", num, (int)frac);
    } else {
        u64_to_str(mb, num, sizeof(num));
        ksprintf(buf, (size_t)bufsz, "%s MB", num);
    }
}

// ---------------- 环境变量 ----------------
bool env_set(const char* key, const char* value) {
    if (!key || !value) return false;
    env_init();
    // 已存在则覆盖
    for (int i = 0; i < ENV_MAX; i++) {
        if (g_env[i].used && strcmp(g_env[i].key, key) == 0) {
            copy_str(g_env[i].val, ENV_VAL, value);
            return true;
        }
    }
    // 找空槽
    for (int i = 0; i < ENV_MAX; i++) {
        if (!g_env[i].used) {
            copy_str(g_env[i].key, ENV_KEY, key);
            copy_str(g_env[i].val, ENV_VAL, value);
            g_env[i].used = true;
            g_env_used++;
            return true;
        }
    }
    return false;
}

const char* env_get(const char* key) {
    if (!key) return 0;
    env_init();
    for (int i = 0; i < ENV_MAX; i++) {
        if (g_env[i].used && strcmp(g_env[i].key, key) == 0)
            return g_env[i].val;
    }
    return 0;
}

bool env_unset(const char* key) {
    if (!key) return false;
    env_init();
    for (int i = 0; i < ENV_MAX; i++) {
        if (g_env[i].used && strcmp(g_env[i].key, key) == 0) {
            g_env[i].used = false;
            g_env[i].key[0] = 0;
            g_env[i].val[0] = 0;
            g_env_used--;
            return true;
        }
    }
    return false;
}

int env_count() {
    env_init();
    int c = 0;
    for (int i = 0; i < ENV_MAX; i++) if (g_env[i].used) c++;
    return c;
}

bool env_at(int idx, char* key, int keysz, char* val, int valsz) {
    env_init();
    int seen = 0;
    for (int i = 0; i < ENV_MAX; i++) {
        if (!g_env[i].used) continue;
        if (seen == idx) {
            copy_str(key, keysz, g_env[i].key);
            copy_str(val, valsz, g_env[i].val);
            return true;
        }
        seen++;
    }
    return false;
}

void env_clear() {
    for (int i = 0; i < ENV_MAX; i++) {
        g_env[i].used = false;
        g_env[i].key[0] = 0;
        g_env[i].val[0] = 0;
    }
    g_env_used = 0;
}

// ---------------- 扩展指标实现 ----------------
namespace {
LoadAvg g_load = {0.0, 0.0, 0.0};
NetStat g_net = {"0.0.0.0", 1500, 0, 0};
DiskPart g_parts[8];
int g_part_n = 0;
} // namespace

LoadAvg sysinfo_loadavg() { return g_load; }
void sysinfo_set_loadavg(double l1, double l5, double l15) {
    g_load.l1 = l1; g_load.l5 = l5; g_load.l15 = l15;
}
NetStat sysinfo_netstat() { return g_net; }
void sysinfo_set_netstat(const NetStat* n) {
    if (!n) return;
    g_net = *n;
}
int sysinfo_disk_parts(DiskPart* out, int max) {
    int n = g_part_n < max ? g_part_n : max;
    for (int i = 0; i < n; i++) out[i] = g_parts[i];
    return n;
}
void sysinfo_disk_parts_add(const DiskPart* p) {
    if (!p || g_part_n >= 8) return;
    g_parts[g_part_n++] = *p;
}
// ---------------- 自检 ----------------
// ---------------- cpufreq ----------------
static CpuFreq g_cpufreq = { 800, 3200, 800, "ondemand" };

CpuFreq sysinfo_cpufreq() { return g_cpufreq; }

void sysinfo_cpufreq_set(const CpuFreq* f) {
    if (!f) return;
    g_cpufreq = *f;
}

uint32_t sysinfo_cpufreq_apply(const CpuFreq* f) {
    if (!f) return 0;
    // performance: p-e; powersave: p-o; ondemand: o
    char g0 = f->governor[0], g1 = f->governor[1];
    if ((g0 == 'p' || g0 == 'P') && (g1 == 'e' || g1 == 'E'))
        return f->max_mhz;                                      // performance
    if (g0 == 'o' || g0 == 'O')
        return (f->min_mhz + f->max_mhz) / 2;                   // ondemand
    return f->min_mhz;                                          // powersave
}
void sysinfo_uptime_breakdown(uint64_t ms, int* days, int* hours, int* mins, int* secs) {
    uint64_t total = ms / 1000;
    int d = (int)(total / 86400); total %= 86400;
    int h = (int)(total / 3600);  total %= 3600;
    int m = (int)(total / 60);    int s = (int)(total % 60);
    if (days) *days = d;
    if (hours) *hours = h;
    if (mins) *mins = m;
    if (secs) *secs = s;
}
MemBreakdown sysinfo_mem_breakdown(const SysInfo* s) {
    MemBreakdown b = {0,0,0};
    if (!s) return b;
    uint64_t used = s->mem_used_mb;
    b.buffers_mb = used / 8;            // 演示：1/8 算 buffer
    b.cached_mb  = used / 4;            // 1/4 算 cache
    uint64_t accounted = b.buffers_mb + b.cached_mb;
    b.free_mb = (s->mem_total_mb > accounted) ? (s->mem_total_mb - accounted) : 0;
    return b;
}
static DiskIO g_diskio = {0, 0, 0};

DiskIO sysinfo_disk_io() { return g_diskio; }

void sysinfo_disk_io_add(uint64_t r, uint64_t w) {
    g_diskio.read_bytes += r;
    g_diskio.write_bytes += w;
}
static char g_cmdline[128] = "nefuOS root=/dev/ram0 quiet";

const char* sysinfo_cmdline() { return g_cmdline; }

void sysinfo_set_cmdline(const char* s) {
    if (!s) return;
    int i = 0;
    for (; s[i] && i < 127; i++) g_cmdline[i] = s[i];
    g_cmdline[i] = 0;
}
const char* sysinfo_version() { return "nefuOS 0.1.0 sysutil"; }
int sysinfo_cpu_count() {
    HwInfo hw;
    platform_hw_info(&hw);
    return hw.cpu_cores > 0 ? hw.cpu_cores : 1;
}
int sysinfo_self_test() {
    int fails = 0;

    // 构造一份确定的 mock 快照
    SysInfo s;
    for (uint32_t i = 0; i < sizeof(s); i++) ((char*)&s)[i] = 0;  // 手动清零
    copy_str(s.cpu_model, sizeof(s.cpu_model), "Nefu-Virtual-Core 80486");
    s.cpu_mhz = 3200;
    s.cpu_cores = 4;
    s.mem_total_mb = 8192;
    s.mem_used_mb = 2048;          // 25%
    s.disk_total_mb = 2048ull * 1024ull;
    s.disk_used_mb = 512ull * 1024ull;
    s.uptime_ms = (86400u * 3u + 3u * 3600u + 25u * 60u + 9u) * 1000u;
    copy_str(s.os_name, sizeof(s.os_name), "nefuOS");
    sysinfo_set_mock(&s);

    SysInfo got;
    sysinfo_refresh(&got);
    if (got.cpu_cores != 4) fails++;
    if (got.cpu_mhz != 3200) fails++;
    if (got.mem_total_mb != 8192) fails++;
    if (strcmp(got.cpu_model, "Nefu-Virtual-Core 80486") != 0) fails++;

    // 内存百分比 = 25%
    double mp = sysinfo_mem_percent(&got);
    if (mp < 24.9 || mp > 25.1) fails++;
    double dp = sysinfo_disk_percent(&got);
    if (dp < 24.9 || dp > 25.1) fails++;

    // uptime 格式化：3 天 03:25:09
    uint32_t sec = sysinfo_uptime_sec(&got);
    char ubuf[32];
    sysinfo_format_uptime(sec, ubuf, sizeof(ubuf));
    // 期望 "3d 03:25:09"
    if (strcmp(ubuf, "3d 03:25:09") != 0) fails++;

    // MB 格式化：2048 MB -> "2.0 GB"
    char fb[32];
    sysinfo_format_mb(2048, fb, sizeof(fb));
    if (strcmp(fb, "2.0 GB") != 0) fails++;
    sysinfo_format_mb(512, fb, sizeof(fb));
    if (strcmp(fb, "512 MB") != 0) fails++;
    sysinfo_format_mb(3ull * 1024ull, fb, sizeof(fb));   // 3GB
    if (strcmp(fb, "3.0 GB") != 0) fails++;

    // 环境变量
    env_clear();
    if (!env_set("EDITOR", "nefedit")) fails++;
    if (!env_set("TERM", "xterm")) fails++;
    if (env_count() != 2) fails++;
    const char* e = env_get("EDITOR");
    if (!e || strcmp(e, "nefedit") != 0) fails++;
    if (env_get("NO_SUCH_VAR") != 0) fails++;
    // 覆盖
    env_set("TERM", "vt100");
    if (env_count() != 2) fails++;     // 覆盖不增加条目
    e = env_get("TERM");
    if (!e || strcmp(e, "vt100") != 0) fails++;
    // unset
    if (!env_unset("EDITOR")) fails++;
    if (env_get("EDITOR") != 0) fails++;
    if (env_count() != 1) fails++;

    // 扩展指标
    sysinfo_set_loadavg(1.5, 0.8, 0.2);
    LoadAvg la = sysinfo_loadavg();
    if (la.l1 < 1.4 || la.l1 > 1.6) fails++;
    if (la.l5 < 0.7 || la.l5 > 0.9) fails++;
    NetStat ns;
    ksprintf(ns.ip, sizeof(ns.ip), "10.0.2.15");
    ns.mtu = 1500; ns.rx_bytes = 1024; ns.tx_bytes = 512;
    sysinfo_set_netstat(&ns);
    NetStat nget = sysinfo_netstat();
    if (nget.mtu != 1500) fails++;
    if (nget.rx_bytes != 1024) fails++;
    DiskPart part;
    copy_str(part.name, sizeof(part.name), "sda1");
    part.size_mb = 1024; part.used_mb = 256;
    sysinfo_disk_parts_add(&part);
    copy_str(part.name, sizeof(part.name), "sda2");
    part.size_mb = 512; part.used_mb = 128;
    sysinfo_disk_parts_add(&part);
    DiskPart parts[4];
    int np = sysinfo_disk_parts(parts, 4);
    if (np != 2) fails++;
    if (parts[0].size_mb != 1024) fails++;
    if (parts[1].used_mb != 128) fails++;

    // cpufreq governor
    CpuFreq cf = { 800, 3200, 800, "performance" };
    if (sysinfo_cpufreq_apply(&cf) != 3200) fails++;
    int i = 0; for (; "powersave"[i]; i++) cf.governor[i] = "powersave"[i]; cf.governor[i] = 0;
    if (sysinfo_cpufreq_apply(&cf) != 800) fails++;
    i = 0; for (; "ondemand"[i]; i++) cf.governor[i] = "ondemand"[i]; cf.governor[i] = 0;
    if (sysinfo_cpufreq_apply(&cf) != 2000) fails++;
    sysinfo_cpufreq_set(&cf);
    if (sysinfo_cpufreq().min_mhz != 800) fails++;

    // 运行时间分解：1天2时3分4秒 = 90064000ms
    int ud,uh,um,us;
    sysinfo_uptime_breakdown(93784000ull, &ud, &uh, &um, &us);
    if (ud != 1 || uh != 2 || um != 3 || us != 4) fails++;

    // 内存细分
    SysInfo ms;
    ms.mem_total_mb = 1000; ms.mem_used_mb = 400;
    MemBreakdown mb = sysinfo_mem_breakdown(&ms);
    if (mb.buffers_mb != 50) fails++;
    if (mb.cached_mb != 100) fails++;

    // 磁盘 IO
    sysinfo_disk_io_add(1024, 512);
    DiskIO dio = sysinfo_disk_io();
    if (dio.read_bytes != 1024) fails++;
    if (dio.write_bytes != 512) fails++;

    // cmdline
    if (nefu::strcmp(sysinfo_cmdline(), "nefuOS root=/dev/ram0 quiet") != 0) fails++;
    sysinfo_set_cmdline("testmode");
    if (nefu::strcmp(sysinfo_cmdline(), "testmode") != 0) fails++;

    sysinfo_set_mock(0);   // 还原
    if (sysinfo_cpu_count() <= 0) fails++;
    if (nefu::strcmp(sysinfo_version(), "nefuOS 0.1.0 sysutil") != 0) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
