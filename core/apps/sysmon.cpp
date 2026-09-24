// nefuOS 系统监视器 —— 窗口应用
// 参考 algoviz.cpp 模式：自包含的状态机，不依赖线程/异常。
// 展示：CPU/内存占用条、进程列表、服务状态。数据来自 core/sysutil。
// 控制：
//   P     : 新建一个演示进程
//   K     : 终止最下面一个进程
//   S     : 刷新服务状态
//   ESC   : 关闭窗口
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sysutil/sysutil_all.h"

namespace nefu {

using namespace nefu::sysutil;

namespace {

const int SM_W = 620, SM_H = 440;

struct SysMon {
    uint32_t tick;
    int      proc_seed;

    void init() {
        tick = 0;
        proc_seed = 1;
        // 造几个演示进程
        g_procman.set_now(0);
        g_procman.create_process("init",    PRIO_REALTIME, 10000, 0);
        g_procman.create_process("nefud",  PRIO_HIGH,     200,   0);
        g_procman.create_process("sysmon",  PRIO_NORMAL,   300,   0);
        g_procman.create_process("shell",   PRIO_NORMAL,   150,   1000);
        // 注册演示服务
        g_svcmgr.reset();
        const char* d0[] = {0};
        const char* netdeps[] = {"network", 0};
        g_svcmgr.register_service("network", d0, true);
        g_svcmgr.register_service("sshd", netdeps, true);
        g_svcmgr.register_service("httpd", netdeps, false);
        g_svcmgr.start("sshd");
        // 初始事件日志
        g_syslog.clear();
        g_syslog.write(LOG_INFO, "sysmon", "monitor started");
        g_syslog.write(LOG_INFO, "network", "link up 1000Mbps");
        g_syslog.write(LOG_WARN, "thermal", "CPU temp 78C");
        // 演示配置
        g_config.clear();
        g_config.set_str("network", "ip", "10.0.2.15");
        g_config.set_int("network", "port", 80);
        g_config.set_bool("system", "debug", false);
    }

    // 虚拟时钟推进一步，跑调度
    void step() {
        tick += 50;
        g_procman.set_now(tick);
        g_procman.advance(50);
        g_svcmgr.watchdog_tick(tick);
        // 每若干拍写一条演示日志
        if ((tick / 50) % 20 == 0)
            g_syslog.write(LOG_DEBUG, "sched", "heartbeat");
    }

    void paint(Surface& s) {
        s.fill(0x00FAF8EF);
        int W = s.width, H = s.height;
        gfx::text_scale(s, 10, 8, "System Monitor", 0x00776756, 0x00FAF8EF, 2);
        char buf[96];
        ksprintf(buf, sizeof(buf), "uptime %u ms   procs=%d   services=%d",
                 tick, g_procman.count(), g_svcmgr.count());
        gfx::text(s, 10, 40, buf, 0x00505050, 0x00FAF8EF);
        // 进程状态分布
        int ready = g_procman.count_state(PROC_READY);
        int blocked = g_procman.count_state(PROC_BLOCKED);
        int running = g_procman.count_state(PROC_RUNNING);
        ksprintf(buf, sizeof(buf), "running=%d  ready=%d  blocked=%d  csw=%d",
                 running, ready, blocked, g_procman.context_switches());
        gfx::text(s, 10, 54, buf, 0x007F8C8D, 0x00FAF8EF);
        // CPU governor / 频率
        CpuFreq cf = sysinfo_cpufreq();
        ksprintf(buf, sizeof(buf), "governor=%s  %u/%u MHz  diskIO=%u  cpus=%d",
                 cf.governor, cf.cur_mhz, cf.max_mhz, sysinfo_disk_io().read_bytes, sysinfo_cpu_count());
        gfx::text(s, 10, 66, buf, 0x0095A5A6, 0x00FAF8EF);

        // ---- 内存占用条（演示值）----
        int bar_x = 10, bar_y = 64, bar_w = W - 20, bar_h = 12;
        gfx::text(s, bar_x, bar_y - 14, "Memory", 0x006B7280, 0x00FAF8EF);
        gfx::fillrect(s, bar_x, bar_y, bar_w, bar_h, 0x00E5E4DC);
        int used = (int)((tick / 50) % 60) + 20;   // 20..80 演示波动
        gfx::fillrect(s, bar_x, bar_y, bar_w * used / 100, bar_h, 0x003498DB);
        ksprintf(buf, sizeof(buf), "%d%%", used);
        gfx::text(s, bar_x + bar_w - 36, bar_y - 2, buf, 0x003498DB, 0x00E5E4DC);

        // ---- 磁盘占用条 ----
        int diskpct = 45;
        gfx::text(s, bar_x, bar_y + 18, "Disk /", 0x006B7280, 0x00FAF8EF);
        gfx::fillrect(s, bar_x, bar_y + 32, bar_w, bar_h, 0x00E5E4DC);
        gfx::fillrect(s, bar_x, bar_y + 32, bar_w * diskpct / 100, bar_h, 0x008E44AD);
        ksprintf(buf, sizeof(buf), "%d%%", diskpct);
        gfx::text(s, bar_x + bar_w - 36, bar_y + 30, buf, 0x008E44AD, 0x00E5E4DC);

        // ---- 进程表 ----
        int ty = 150;
        gfx::text(s, 10, ty, "PID  NAME        PRIO  STATE  CPU", 0x006B7280, 0x00FAF8EF);
        ty += 16;
        int shown = g_procman.count();
        if (shown > 12) shown = 12;
        for (int i = 0; i < shown; i++) {
            char line[96];
            g_procman.describe(i, line, sizeof(line));
            uint32_t col = (i % 2) ? 0x00F0EFE8 : 0x00FFFFFF;
            gfx::fillrect(s, 8, ty - 2, W - 16, 14, col);
            gfx::text(s, 12, ty, line, 0x00333333, col);
            ty += 15;
        }

        // ---- 服务状态 ----
        ty += 6;
        gfx::text(s, 10, ty, "Services", 0x006B7280, 0x00FAF8EF);
        ty += 16;
        for (int i = 0; i < g_svcmgr.count(); i++) {
            const Service* sv = g_svcmgr.at(i);
            const char* st = "STOPPED";
            switch (sv->state) {
            case SVC_STARTING: st = "STARTING"; break;
            case SVC_RUNNING: st = "RUNNING"; break;
            case SVC_STOPPING: st = "STOPPING"; break;
            case SVC_FAILED: st = "FAILED"; break;
            default: break;
            }
            uint32_t col = (sv->state == SVC_RUNNING) ? 0x0027AE60 :
                           (sv->state == SVC_FAILED ? 0x00E74C3C : 0x00808080);
            ksprintf(buf, sizeof(buf), "  %-12s  %s  restarts=%d",
                     sv->name, st, sv->restart_count);
            gfx::text(s, 12, ty, buf, col, 0x00FAF8EF);
            ty += 15;
        }

        // ---- 最近事件日志（从 g_syslog 拉最近几条）----
        ty += 6;
        gfx::text(s, 10, ty, "Recent Events", 0x006B7280, 0x00FAF8EF);
        ty += 16;
        LogEntry rec[4];
        int rn = g_syslog.tail(4, rec);
        for (int i = 0; i < rn; i++) {
            ksprintf(buf, sizeof(buf), "  [%s] %s: %s",
                     log_level_name(rec[i].level), rec[i].src, rec[i].msg);
            uint32_t col = 0x00555555;
            if (rec[i].level == LOG_WARN) col = 0x00B7950B;
            if (rec[i].level >= LOG_ERROR) col = 0x00C0392B;
            gfx::text(s, 12, ty, buf, col, 0x00FAF8EF);
            ty += 14;
        }

        // ---- 当前登录用户（演示）----
        ty += 6;
        gfx::text(s, 10, ty, "Sessions / Users", 0x006B7280, 0x00FAF8EF);
        ty += 16;
        ksprintf(buf, sizeof(buf), "  root     tty1   console   idle 0m");
        gfx::text(s, 12, ty, buf, 0x00333333, 0x00FAF8EF);
        ty += 14;
        ksprintf(buf, sizeof(buf), "  user     pts/0   ssh       idle 3m");
        gfx::text(s, 12, ty, buf, 0x00333333, 0x00FAF8EF);
        ty += 18;

        // ---- 配置摘要 ----
        gfx::text(s, 10, ty, "Config", 0x006B7280, 0x00FAF8EF);
        ty += 16;
        for (int i = 0; i < g_config.count() && i < 4; i++) {
            const ConfigEntry* e = g_config.at(i);
            ksprintf(buf, sizeof(buf), "  %s.%s = %s", e->section.c_str(),
                     e->key.c_str(), e->value.c_str());
            gfx::text(s, 12, ty, buf, 0x00555555, 0x00FAF8EF);
            ty += 14;
        }

        ksprintf(buf, sizeof(buf), "%s  |  csw=%d  |  procs=%d  |  svcs=%d",
                 sysinfo_version(), g_procman.context_switches(),
                 g_procman.count(), g_svcmgr.count());
        gfx::text(s, 10, H - 30, buf, 0x00AAAAAA, 0x00FAF8EF);
        gfx::text(s, 10, H - 16, "P:new proc  K:kill last  S:restart svc  L:log event  Esc:close",
                  0x00909090, 0x00FAF8EF);
        gfx::text(s, 10, H - 4, "nefuOS sysutil sysmonitor v0.1", 0x00606060, 0x00FAF8EF);
    }
};

} // namespace

static SysMon* sm_of(Window* w) { return (SysMon*)w->userdata; }

static void sm_paint(Window* w) { sm_of(w)->paint(w->back); }
static void sm_tick(Window* w) {
    SysMon* m = sm_of(w);
    if (!m) return;
    m->step();
}
static void sm_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    SysMon* m = sm_of(w);
    if (e->ascii == 'p' || e->ascii == 'P') {
        char name[16];
        ksprintf(name, sizeof(name), "app%d", m->proc_seed++);
        g_procman.create_process(name, PRIO_NORMAL, 50 + (m->proc_seed % 5) * 20, 1000);
        return;
    }
    if (e->ascii == 'k' || e->ascii == 'K') {
        int n = g_procman.count();
        if (n > 1) {
            // 取表中最后一个存活进程的 pid 并终止
            int last_pid = -1;
            for (int i = 0; i < n; i++) {
                // describe 不暴露 pid，这里用遍历 pid 的方式找最后一个
                // 简化：pid 从 1 递增，直接找当前最大 pid
            }
            // 直接扫描：从高 pid 往低找第一个存在的
            for (int pid = 64; pid >= 1; pid--) {
                if (g_procman.find(pid)) { last_pid = pid; break; }
            }
            if (last_pid > 0) g_procman.terminate(last_pid);
        }
        return;
    }
    if (e->ascii == 's' || e->ascii == 'S') {
        g_svcmgr.start("httpd");
        return;
    }
    if (e->ascii == 'l' || e->ascii == 'L') {
        g_syslog.write(LOG_INFO, "user", "manual event");
        return;
    }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}
static void sm_close(Window* w) {
    if (w->userdata) delete (SysMon*)w->userdata;
    w->userdata = 0;
}

void sysmon_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("System Monitor", x, y, SM_W, SM_H);
    if (!w) return;
    SysMon* m = new SysMon();
    m->init();
    w->userdata = m;
    w->on_paint = sm_paint;
    w->on_key = sm_key;
    w->on_tick = sm_tick;
    w->on_close = sm_close;
    g_wm->raise(w);
}

} // namespace nefu
// sysmon 窗口应用结束
