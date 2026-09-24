// nefuOS 系统工具扩展库 —— 系统信息模块
// 汇总 CPU / 内存 / 磁盘 / 运行时间 / 环境变量，并提供人类可读的格式化。
// 真实环境下数据来自 platform 层（platform_hw_info / platform_mem_stats /
// platform_tick_ms）；测试与无平台环境下可通过 sysinfo_set_mock() 注入假数据。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

// 一份快照式系统信息。所有字段都是 POD 或定长数组，便于拷贝与序列化。
struct SysInfo {
    char     cpu_model[64];     // CPU 型号字符串
    uint32_t cpu_mhz;           // 主频 MHz
    uint8_t  cpu_cores;         // 逻辑核心数
    uint64_t mem_total_mb;      // 物理内存总量(MB)
    uint64_t mem_used_mb;       // 已用内存(MB)
    uint64_t disk_total_mb;     // 根分区总量(MB)
    uint64_t disk_used_mb;      // 根分区已用(MB)
    uint64_t uptime_ms;         // 系统启动至今的毫秒数
    char     os_name[32];       // 操作系统名
    char     build_tag[32];     // 构建标签
};

// 刷新一次快照：从 platform 层读取真实硬件/内存数据。
// 在没有 platform 实现的宿主测试里，会自动回退到 mock 数据。
void sysinfo_refresh(SysInfo* out);

// 用一组确定的假数据覆盖内部缓存（self_test / sysmon 演示用）。
void sysinfo_set_mock(const SysInfo* m);

// 派生指标 ---------------------------------------------------------------
double sysinfo_mem_percent(const SysInfo* s);    // 0..100
double sysinfo_disk_percent(const SysInfo* s);    // 0..100
uint32_t sysinfo_uptime_sec(const SysInfo* s);   // 秒
// 把秒数格式化成 "Xd HH:MM:SS" 写到 buf（bufsz >= 24）。
void sysinfo_format_uptime(uint32_t total_sec, char* buf, int bufsz);
// 把 MB 数格式化成人类可读串（"1.5 GB" / "512 MB"）。ksprintf 不支持 %f，
// 所以这里手工处理小数。
void sysinfo_format_mb(uint64_t mb, char* buf, int bufsz);

// ---------------- 环境变量 ----------------
// 一个极简的键值表（区分大小写）。用于模拟 PATH / HOME / USER 等环境。
// 上限 ENV_MAX 条，key/value 各限 ENV_KEY/ENV_VAL 字节。
const int ENV_MAX = 64;
const int ENV_KEY = 32;
const int ENV_VAL = 96;

// 设置（新增或覆盖）一个环境变量。返回 false 表示表满或参数非法。
bool env_set(const char* key, const char* value);
// 读取环境变量；不存在返回 0。
const char* env_get(const char* key);
// 删除一个环境变量；不存在返回 false。
bool env_unset(const char* key);
// 表项数量。
int  env_count();
// 按下标导出一对键值（用于遍历 / sysmon 展示）。
bool env_at(int idx, char* key, int keysz, char* val, int valsz);
// 清空全部环境变量。
void env_clear();

// ---------------- 扩展指标 ----------------
// 1/5/15 分钟负载均值（0..100 百分比，模拟）。
struct LoadAvg { double l1, l5, l15; };
LoadAvg sysinfo_loadavg();
void    sysinfo_set_loadavg(double l1, double l5, double l15);

// 网络接口摘要：导出活动适配器 IP/MTU/连接数。
struct NetStat {
    char     ip[16];
    uint32_t mtu;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
};
NetStat  sysinfo_netstat();
void     sysinfo_set_netstat(const NetStat* n);

// 磁盘分区表：追加至多 max 个分区到 out，返回实际数量。
struct DiskPart {
    char     name[16];
    uint64_t size_mb;
    uint64_t used_mb;
};
int      sysinfo_disk_parts(DiskPart* out, int max);
void     sysinfo_disk_parts_add(const DiskPart* p);   // 测试用注册

// CPU 频率调节（cpufreq governor）-----------------------------------------
// 模拟各核心的频率档位。governor: "powersave"/"performance"/"ondemand"。
struct CpuFreq {
    uint32_t min_mhz;
    uint32_t max_mhz;
    uint32_t cur_mhz;
    char     governor[16];
};
CpuFreq sysinfo_cpufreq();
void    sysinfo_cpufreq_set(const CpuFreq* f);
// 按 governor 算出 cur_mhz：powersave=min, performance=max, ondemand=折中。
uint32_t sysinfo_cpufreq_apply(const CpuFreq* f);

// 运行时间分解：把毫秒拆成天/时/分/秒。
void sysinfo_uptime_breakdown(uint64_t ms, int* days, int* hours, int* mins, int* secs);

// 内存细分：返回 free/cached/buffer 字节数（演示值，基于已用内存推算）。
struct MemBreakdown { uint64_t free_mb; uint64_t cached_mb; uint64_t buffers_mb; };
MemBreakdown sysinfo_mem_breakdown(const SysInfo* s);

// 磁盘 IO 计数器（累计）。
struct DiskIO { uint64_t read_bytes; uint64_t write_bytes; uint32_t ios_inflight; };
DiskIO sysinfo_disk_io();
void   sysinfo_disk_io_add(uint64_t r, uint64_t w);

// 内核启动命令行参数（演示串）。
const char* sysinfo_cmdline();
void        sysinfo_set_cmdline(const char* s);

// nefuOS 版本串。
const char* sysinfo_version();
// CPU 核心数。
int sysinfo_cpu_count();

// 自检
int sysinfo_self_test();

} // namespace sysutil
} // namespace nefu
