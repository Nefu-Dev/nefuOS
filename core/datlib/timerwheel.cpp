// nefuOS data-types library — timerwheel implementation & helpers
#include "timerwheel.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 用时间轮模拟"超时检测"：返回第一次出现超时的事件下标。
// events 为 {到达时间, 超时阈值}；对每个事件注册定时器并推进。
// 简化演示：只注册一个最长定时器并推进到它触发。
int timerwheel_demo_timeout(int max_ms) {
    timerwheel tw;
    int id = tw.add_timer(max_ms);
    if (id < 0) return -1;
    int fired[8];
    int ticks = 0;
    while (ticks <= max_ms + 1) {
        if (tw.tick(fired, 8) > 0) return ticks + 1;   // 第 ticks+1 次 tick = 已过 ticks+1 ms
        ticks++;
    }
    return -1;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int timerwheel_self_test() {
    g_fails = 0;
    {
        timerwheel tw;
        expect("tw-empty", tw.timer_count() == 0);
        int a = tw.add_timer(5);
        int b = tw.add_timer(3);
        int c = tw.add_timer(10);
        expect("tw-ids", a > 0 && b > 0 && c > 0);
        expect("tw-count", tw.timer_count() == 3);
        // 推进 3ms：b(3ms) 到期
        int fired[8];
        int n = 0;
        for (int i = 0; i < 3; i++) n += tw.tick(fired, 8);
        expect("tw-first", n == 1);
        expect("tw-fired-id", fired[0] == b);
        expect("tw-count2", tw.timer_count() == 2);
        // 再推进 2ms：a(5ms) 到期
        n = 0;
        for (int i = 0; i < 2; i++) n += tw.tick(fired, 8);
        expect("tw-second", n == 1 && fired[0] == a);
        // 取消 c
        expect("tw-cancel", tw.cancel(c));
        expect("tw-cancel2", !tw.cancel(c));
        expect("tw-count3", tw.timer_count() == 0);
        // 取消后不再触发
        int d = tw.add_timer(2);
        expect("tw-cancel3", tw.cancel(d));
        n = 0;
        for (int i = 0; i < 5; i++) n += tw.tick(fired, 8);
        expect("tw-cancelled", n == 0);
    }
    {
        // 多个定时器同时到期
        timerwheel tw;
        int id1 = tw.add_timer(4);
        int id2 = tw.add_timer(4);
        int id3 = tw.add_timer(4);
        int fired[16];
        int n = 0;
        for (int i = 0; i < 4; i++) n += tw.tick(fired, 16);
        expect("tw-multi", n == 3);
        bool ok = false;
        for (int i = 0; i < n; i++) if (fired[i] == id1) ok = true;
        expect("tw-multi-id", ok);
        expect("tw-multi-empty", tw.timer_count() == 0);
    }
    {
        // 时间轮驱动演示
        expect("tw-demo", timerwheel_demo_timeout(100) == 100);
        expect("tw-demo2", timerwheel_demo_timeout(7) == 7);
        // 容量限制
        timerwheel big;
        int count_ok = 0;
        for (int i = 0; i < TW_MAX_TIMERS + 10; i++) {
            if (big.add_timer(1000) >= 0) count_ok++;
        }
        expect("tw-cap", count_ok == TW_MAX_TIMERS);
        int fired2[4];
        int nn = 0;
        for (int i = 0; i < 1000; i++) nn += big.tick(fired2, 4);
        expect("tw-all-fired", nn == TW_MAX_TIMERS);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
