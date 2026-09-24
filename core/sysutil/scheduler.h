// nefuOS 系统工具扩展库 —— 任务调度器
// 支持三类任务：
//   1) 一次性任务(ONESHOT)：到点跑一次，然后移除；
//   2) 周期任务(PERIODIC)：每隔固定毫秒跑一次；
//   3) cron 任务(CRON)：按标准 5 段式 "分 时 日 月 周" 表达式触发。
// 调度器由虚拟时钟驱动：调用 set_tick(now_ms) 后，tick() 返回这一时刻到期的
// 任务列表，由调用方执行。所有时间均为虚拟毫秒，便于确定性测试。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

enum TaskType {
    TASK_ONESHOT = 0,
    TASK_PERIODIC,
    TASK_CRON
};

// cron 字段掩码：每个字段用一个 64 位位图表示命中的取值集合。
// 分(0-59)、时(0-23)、日(1-31)、月(1-12)、周(0-6, 0=周日)。
struct CronExpr {
    uint64_t minute;    // 位 n 置位 = 该分命中
    uint32_t hour;      // 位 n
    uint32_t dom;       // 位 n（1..31，位 0 不用）
    uint16_t month;     // 位 n（1..12）
    uint8_t  dow;       // 位 n（0..6）
    bool     valid;     // 解析是否成功
};

// 解析 "m h dom mon dow" 五段表达式。支持：
//   * ,  n  a-b  */step  a-b/step  n1,n2
// 解析成功返回 true 并填充 out。
bool cron_parse(const char* expr, CronExpr* out);
// 判断某时刻（分/时/日/月/周）是否命中 cron 表达式。
bool cron_match(const CronExpr* c, int minute, int hour, int dom, int month, int dow);

// ---------------- 调度任务 ----------------
struct ScheduledTask {
    int       id;
    String    name;
    TaskType  type;
    uint32_t  interval_ms;   // PERIODIC 间隔；ONESHOT 首次触发时刻
    uint32_t  next_run_ms;   // 下一次触发的虚拟毫秒
    int       run_count;      // 已触发次数
    bool      enabled;
    bool      paused;         // 暂停标记
    int       max_runs;       // 最大触发次数（0 = 无限）
    char      command[64];    // 任务命令描述
    CronExpr  cron;          // TASK_CRON 时使用
};

// 到期事件
struct FiredTask {
    int task_id;
    const char* task_name;
};

class Scheduler {
public:
    Scheduler();

    // 添加任务。ONESHOT: delay_ms 后跑一次；PERIODIC: 每 interval_ms 跑；
    // CRON: 解析 cron_expr（失败返回 -1）。返回任务 id（<0 失败）。
    int add_oneshot(const char* name, uint32_t delay_ms);
    int add_periodic(const char* name, uint32_t interval_ms);
    int add_cron(const char* name, const char* cron_expr);

    bool remove_task(int id);
    bool enable_task(int id, bool on);
    // 暂停 / 恢复：暂停后 tick 不再触发该任务。
    bool pause_task(int id);
    bool resume_task(int id);
    // 任务历史：返回某任务累计触发次数（run_count）。
    int  history_count(int id) const;
    // 暂停的任务数。
    int  paused_count() const;
    // 设置任务命令描述与最大触发次数。
    bool set_command(int id, const char* cmd);
    bool set_max_runs(int id, int max);
    // 任务命令（只读）。
    const char* command(int id) const;
    // 统计摘要：任务总数、累计触发总次数。
    void stats(int* total_tasks, long* total_fired) const;
    int  count() const { return tasks_.size(); }
    const ScheduledTask* find(int id) const;

    // 推进虚拟时钟到 now_ms，触发所有到期任务，把任务 id 写入 out（最多 max 条）。
    // 返回实际触发条数。触发后 PERIODIC/CRON 自动重排下一次。
    int  tick(uint32_t now_ms, FiredTask* out, int max);

    void clear();

    // 自检
    int self_test();

private:
    ScheduledTask* find_mut(int id);
    int alloc_id();
    // 计算 cron 任务从 now 起下一次命中的毫秒偏移（以 1 分钟为粒度近似）。
    uint32_t next_cron_delay(const CronExpr* c, uint32_t now_ms) const;

    List<ScheduledTask> tasks_;
    int next_id_;
};

// 任务类型名
const char* task_type_name(TaskType t);

// 全局调度器
extern Scheduler g_scheduler;

} // namespace sysutil
} // namespace nefu
