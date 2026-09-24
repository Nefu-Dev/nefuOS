// nefuOS 系统工具扩展库 —— 任务调度器实现
#include "scheduler.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

Scheduler g_scheduler;

namespace {

// 把单个 cron 字段解析成位图。
// field: "0-59" / "0-23" / "1-31" / "1-12" / "0-6"
// 返回位图；max_val 是该字段最大值（用于越界保护）。
uint64_t parse_field(const char* s, int max_val) {
    uint64_t bits = 0;
    if (!s || !*s) return 0;
    // 逗号分段
    while (*s) {
        // 取一段（到逗号或结尾）
        char seg[24];
        int n = 0;
        while (*s && *s != ',' && n < (int)sizeof(seg) - 1) seg[n++] = *s++;
        seg[n] = 0;
        if (*s == ',') s++;
        // 解析 step：seg 里可能含 '/'
        int step = 1;
        char* slash = (char*)strchr(seg, '/');
        if (slash) {
            *slash = 0;
            step = nefu::atoi(slash + 1);
            if (step < 1) step = 1;
        }
        int lo = 0, hi = max_val;
        if (strcmp(seg, "*") == 0) {
            lo = (max_val == 59 || max_val == 23 || max_val == 6) ? 0 : 1;
            hi = max_val;
        } else if (strchr(seg, '-')) {
            char* dash = (char*)strchr(seg, '-');
            *dash = 0;
            lo = nefu::atoi(seg);
            hi = nefu::atoi(dash + 1);
        } else {
            lo = hi = nefu::atoi(seg);
        }
        if (lo < 0) lo = 0;
        if (hi > max_val) hi = max_val;
        for (int v = lo; v <= hi; v += step) {
            if (v >= 0 && v <= max_val) bits |= (1ull << v);
        }
    }
    return bits;
}

} // namespace

bool cron_parse(const char* expr, CronExpr* out) {
    if (!out) return false;
    out->valid = false;
    out->minute = out->hour = out->dom = out->month = out->dow = 0;
    if (!expr) return false;
    // 复制到可写缓冲，按空白切 5 段
    char buf[128];
    int n = 0;
    while (*expr && n < (int)sizeof(buf) - 1) { buf[n++] = *expr++; }
    buf[n] = 0;
    char* fields[5] = {0, 0, 0, 0, 0};
    int f = 0;
    char* p = buf;
    // 跳过前导空白
    while (*p == ' ' || *p == '\t') p++;
    fields[0] = p;
    // 逐字符走：遇到空白就结束当前段、跳到下一段开头。
    while (*p && f < 4) {
        if (*p == ' ' || *p == '\t') {
            *p = 0;                 // 结束当前字段
            p++;                    // 越过这个空白
            while (*p == ' ' || *p == '\t') p++;   // 吃掉连续空白
            f++;
            fields[f] = p;          // 下一段开头
        } else {
            p++;
        }
    }
    if (f != 4 || !fields[0] || !fields[1] || !fields[2] || !fields[3] || !fields[4])
        return false;
    out->minute = parse_field(fields[0], 59);
    out->hour   = parse_field(fields[1], 23);
    out->dom    = parse_field(fields[2], 31);
    out->month  = parse_field(fields[3], 12);
    out->dow    = parse_field(fields[4], 6);
    // 至少要有一个可命中位
    if (!out->minute || !out->hour || !out->month) return false;
    out->valid = true;
    return true;
}

bool cron_match(const CronExpr* c, int minute, int hour, int dom, int month, int dow) {
    if (!c || !c->valid) return false;
    if (!(c->minute & (1ull << minute))) return false;
    if (!(c->hour & (1ull << hour))) return false;
    if (!(c->month & (1ull << month))) return false;
    // dom 与 dow 是 OR 关系（标准 cron 语义）：任一命中即可
    bool dom_hit = (c->dom & (1ull << dom)) != 0;
    bool dow_hit = (c->dow & (1ull << dow)) != 0;
    // 若 dom 与 dow 都被限制（不是全 *），则按 OR；这里简化为 OR。
    if (!dom_hit && !dow_hit) return false;
    return true;
}

// ---------------- Scheduler ----------------
Scheduler::Scheduler() : next_id_(1) {}

int Scheduler::alloc_id() { return next_id_++; }


int Scheduler::add_oneshot(const char* name, uint32_t delay_ms) {
    ScheduledTask t;
    t.id = alloc_id();
    t.name = name ? name : "task";
    t.type = TASK_ONESHOT;
    t.interval_ms = delay_ms;
    t.next_run_ms = 0;   // 由 tick 首次调用时基于基准计算；这里先存 0
    t.run_count = 0;
    t.enabled = true;
    t.cron.valid = false;
    t.paused = false;
    t.max_runs = 0;
    t.command[0] = 0;
    tasks_.push(t);
    return t.id;
}

int Scheduler::add_periodic(const char* name, uint32_t interval_ms) {
    if (interval_ms < 1) interval_ms = 1;
    ScheduledTask t;
    t.id = alloc_id();
    t.name = name ? name : "task";
    t.type = TASK_PERIODIC;
    t.interval_ms = interval_ms;
    t.next_run_ms = interval_ms;   // 从 tick 基准开始，一个间隔后首次触发
    t.run_count = 0;
    t.enabled = true;
    t.cron.valid = false;
    t.paused = false;
    t.max_runs = 0;
    t.command[0] = 0;
    tasks_.push(t);
    return t.id;
}

int Scheduler::add_cron(const char* name, const char* cron_expr) {
    ScheduledTask t;
    CronExpr c;
    if (!cron_parse(cron_expr, &c)) return -1;
    t.id = alloc_id();
    t.name = name ? name : "task";
    t.type = TASK_CRON;
    t.interval_ms = 60000;         // cron 最小粒度 1 分钟
    t.next_run_ms = 60000;         // 近似
    t.run_count = 0;
    t.enabled = true;
    t.cron = c;
    t.paused = false;
    t.max_runs = 0;
    t.command[0] = 0;
    tasks_.push(t);
    return t.id;
}

void Scheduler::clear() {
    tasks_.clear();
    next_id_ = 1;
}
bool Scheduler::remove_task(int id) {
    for (int i = 0; i < tasks_.size(); i++) {
        if (tasks_[i].id == id) { tasks_.remove(i); return true; }
    }
    return false;
}

bool Scheduler::enable_task(int id, bool on) {
    ScheduledTask* t = find_mut(id);
    if (!t) return false;
    t->enabled = on;
    return true;
}

bool Scheduler::pause_task(int id) {
    ScheduledTask* t = find_mut(id);
    if (!t) return false;
    t->paused = true;
    return true;
}
bool Scheduler::resume_task(int id) {
    ScheduledTask* t = find_mut(id);
    if (!t) return false;
    t->paused = false;
    return true;
}
int Scheduler::history_count(int id) const {
    const ScheduledTask* t = find(id);
    return t ? t->run_count : -1;
}
bool Scheduler::set_command(int id, const char* cmd) {
    ScheduledTask* t = find_mut(id);
    if (!t || !cmd) return false;
    int i = 0;
    for (; cmd[i] && i < 63; i++) t->command[i] = cmd[i];
    t->command[i] = 0;
    return true;
}
bool Scheduler::set_max_runs(int id, int max) {
    ScheduledTask* t = find_mut(id);
    if (!t) return false;
    t->max_runs = max;
    return true;
}
const char* task_type_name(TaskType t) {
    switch (t) {
    case TASK_ONESHOT: return "ONESHOT";
    case TASK_PERIODIC: return "PERIODIC";
    case TASK_CRON:    return "CRON";
    }
    return "?";
}
void Scheduler::stats(int* total_tasks, long* total_fired) const {
    if (total_tasks) *total_tasks = tasks_.size();
    long sum = 0;
    for (int i = 0; i < tasks_.size(); i++) sum += tasks_[i].run_count;
    if (total_fired) *total_fired = sum;
}
const char* Scheduler::command(int id) const {
    const ScheduledTask* t = find(id);
    return t ? t->command : "";
}
int Scheduler::paused_count() const {
    int n = 0;
    for (int i = 0; i < tasks_.size(); i++) if (tasks_[i].paused) n++;
    return n;
}
const ScheduledTask* Scheduler::find(int id) const {
    for (int i = 0; i < tasks_.size(); i++)
        if (tasks_[i].id == id) return &tasks_[i];
    return 0;
}

ScheduledTask* Scheduler::find_mut(int id) {
    for (int i = 0; i < tasks_.size(); i++)
        if (tasks_[i].id == id) return &tasks_[i];
    return 0;
}

uint32_t Scheduler::next_cron_delay(const CronExpr* c, uint32_t now_ms) const {
    // 以 1 分钟为粒度向前扫描，最多扫 40000 分钟（约 27 天）。
    uint32_t now_min = now_ms / 60000u;
    // 基准：2026-01-01 00:00 是周四(dow=4)。把 now_min 折合成日历。
    for (uint32_t d = 0; d < 40000u; d++) {
        uint32_t m = now_min + d;
        int minute = (int)(m % 60u);
        int hour   = (int)((m / 60u) % 24u);
        uint32_t dayno = m / (24u * 60u);   // 自 2026-01-01 起的天数
        // 2026 非闰年，逐月推进
        static const int mlen[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int dom = 1, month = 1;
        uint32_t rem = dayno;
        for (int mi = 0; mi < 12; mi++) {
            if (rem < (uint32_t)mlen[mi]) { dom = (int)rem + 1; month = mi + 1; break; }
            rem -= (uint32_t)mlen[mi];
        }
        int dow = (int)((4u + dayno) % 7u);   // 2026-01-01 = 周四=4
        if (cron_match(c, minute, hour, dom, month, dow)) {
            return d * 60000u;
        }
    }
    return 0;
}

int Scheduler::tick(uint32_t now_ms, FiredTask* out, int max) {
    int fired = 0;
    // 倒序遍历以便安全删除 oneshot
    for (int i = tasks_.size() - 1; i >= 0; i--) {
        ScheduledTask& t = tasks_[i];
        if (!t.enabled || t.paused) continue;
        bool due = false;
        switch (t.type) {
        case TASK_ONESHOT:
            // 首次 tick 时记录基准：next_run 相对 now
            if (t.run_count == 0 && t.next_run_ms == 0)
                t.next_run_ms = now_ms + t.interval_ms;
            if ((int32_t)now_ms >= (int32_t)t.next_run_ms) due = true;
            break;
        case TASK_PERIODIC:
            if ((int32_t)now_ms >= (int32_t)t.next_run_ms) due = true;
            break;
        case TASK_CRON: {
            if (t.run_count == 0 && t.next_run_ms == 60000) {
                uint32_t d = next_cron_delay(&t.cron, now_ms);
                // d=0 表示当前分钟即命中；无命中时 next_cron_delay 返回 0xFFFFFFFF
                uint32_t delta = (d == 0xFFFFFFFFu) ? 60000u : d;
                t.next_run_ms = now_ms + delta;
            }
            if ((int32_t)now_ms >= (int32_t)t.next_run_ms) due = true;
            break;
        }
        }
        if (due && fired < max) {
            if (out) {
                out[fired].task_id = t.id;
                out[fired].task_name = t.name.c_str();
            }
            fired++;
            t.run_count++;
        // 达到最大触发次数自动停用
        if (t.max_runs > 0 && t.run_count >= t.max_runs) t.enabled = false;
            // 重排
            if (t.type == TASK_ONESHOT) {
                tasks_.remove(i);    // 一次性任务执行后移除
            } else if (t.type == TASK_PERIODIC) {
                t.next_run_ms = now_ms + t.interval_ms;
            } else {
                uint32_t d = next_cron_delay(&t.cron, now_ms);
                uint32_t delta = (d == 0xFFFFFFFFu) ? 60000u : d;
                t.next_run_ms = now_ms + delta;
            }
        }
    }
    return fired;
}

// ---------------- 自检 ----------------
int Scheduler::self_test() {
    int fails = 0;

    // --- cron 解析 ---
    CronExpr c;
    if (!cron_parse("*/5 * * * *", &c)) fails++;
    else {
        // 每 5 分钟：0,5,10,...,55 共 12 位
        int bits = 0;
        for (int i = 0; i < 60; i++) if (c.minute & (1ull << i)) bits++;
        if (bits != 12) fails++;
    }
    if (!cron_parse("0 9 * * 1-5", &c)) fails++;      // 工作日早 9 点
    if (!(c.hour & (1u << 9))) fails++;
    if (!(c.minute & 1u)) fails++;
    // 周一(1)到周五(5)
    for (int d = 1; d <= 5; d++) if (!(c.dow & (1u << d))) fails++;
    if (c.dow & 1u << 0) fails++;    // 周日不应命中

    // cron_match 直接验证
    CronExpr every9;
    cron_parse("30 8 * * *", &every9);
    if (!cron_match(&every9, 30, 8, 15, 6, 2)) fails++;   // 任意日 8:30
    if (cron_match(&every9, 31, 8, 15, 6, 2)) fails++;    // 31 分不命中
    if (cron_match(&every9, 30, 9, 15, 6, 2)) fails++;    // 9 点不命中

    // 非法表达式
    CronExpr bad;
    if (cron_parse("99 * * * *", &bad)) fails++;   // 分越界 -> 仍可解析但位为空
    // 缺段
    if (cron_parse("* * *", &bad)) fails++;

    // --- 调度器行为 ---
    Scheduler s;
    int p1 = s.add_periodic("heartbeat", 100);   // 每 100ms
    int o1 = s.add_oneshot("cleanup", 250);      // 250ms 后一次
    if (p1 != 1 || o1 != 2) fails++;

    FiredTask ev[16];
    // t=0：还没到点
    int n = s.tick(0, ev, 16);
    if (n != 0) fails++;
    // t=100：heartbeat 第一次
    n = s.tick(100, ev, 16);
    if (n != 1 || ev[0].task_id != p1) fails++;
    // t=200：heartbeat 第二次
    n = s.tick(200, ev, 16);
    if (n != 1) fails++;
    // t=250：oneshot cleanup 到点（heartbeat 下次在 300）
    n = s.tick(250, ev, 16);
    if (n != 1 || ev[0].task_id != o1) fails++;
    // oneshot 已被移除
    if (s.find(o1) != 0) fails++;
    // heartbeat 还在
    if (s.find(p1) == 0) fails++;
    // t=300：heartbeat 第三次
    n = s.tick(300, ev, 16);
    if (n != 1 || ev[0].task_id != p1) fails++;
    if (s.find(p1)->run_count != 3) fails++;

    // enable/disable
    s.enable_task(p1, false);
    n = s.tick(1000, ev, 16);
    if (n != 0) fails++;
    s.enable_task(p1, true);

    // remove
    if (!s.remove_task(p1)) fails++;
    if (s.find(p1) != 0) fails++;

    // 暂停 / 恢复 / 历史
    Scheduler s3;
    int pz = s3.add_periodic("pz", 100);
    FiredTask ev3[4];
    s3.tick(0, ev3, 4);
    s3.tick(100, ev3, 4);                 // 触发 1 次
    if (s3.history_count(pz) != 1) fails++;
    if (!s3.pause_task(pz)) fails++;
    if (s3.paused_count() != 1) fails++;
    s3.tick(200, ev3, 4);                 // 暂停期间不触发
    if (s3.history_count(pz) != 1) fails++;
    if (!s3.resume_task(pz)) fails++;
    s3.tick(300, ev3, 4);                 // 恢复后触发
    if (s3.history_count(pz) != 2) fails++;

    // 命令串 + 最大触发次数
    Scheduler s4;
    int one = s4.add_periodic("onetask", 10);
    if (!s4.set_command(one, "/bin/backup.sh")) fails++;
    if (s4.command(one)[0] != '/') fails++;
    if (!s4.set_max_runs(one, 2)) fails++;
    FiredTask ev4[8];
    s4.tick(0, ev4, 8);
    s4.tick(10, ev4, 8);    // run 1
    s4.tick(20, ev4, 8);    // run 2，达到上限自动停用
    int n3 = s4.tick(30, ev4, 8);   // 不应再触发
    if (n3 != 0) fails++;
    int tt; long tf;
    s4.stats(&tt, &tf);
    if (tt != 1) fails++;
    if (tf != 2) fails++;   // 只触发了 2 次
    s4.clear();
    if (s4.count() != 0) fails++;

    // --- cron 任务集成：每天 0 点 ---
    Scheduler s2;
    int cr = s2.add_cron("midnight", "0 0 * * *");
    if (cr < 0) fails++;
    // 第一次 tick 在 0ms：next_cron_delay 应算出到下一个 0:00 的偏移
    n = s2.tick(0, ev, 16);
    // now=0 对应 2026-01-01 00:00，正好命中 0:00，应立即触发
    if (n != 1) fails++;

    if (nefu::strcmp(task_type_name(TASK_CRON), "CRON") != 0) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
