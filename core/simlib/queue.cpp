// nefuOS simlib —— 离散事件模拟器实现 + 自测
#include "simlib/queue.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace nefu {
namespace simx {

EventSim::EventSim() {}

void EventSim::schedule(double t, int kind, int data) {
    SimEvent e(t, kind, data);
    // 教学版线性插入：保持 time 升序（等价于最小堆的简单实现）
    size_t i = 0;
    while (i < events.size() && events[i].time <= t) i++;
    events.insert(events.begin() + (long)i, e);
}

bool EventSim::pop_next(SimEvent& ev) {
    if (events.empty()) return false;
    ev = events[0];
    events.erase(events.begin());
    return true;
}

double EventSim::peek_time() const {
    if (events.empty()) return -1;
    return events[0].time;
}

void EventSim::clear() { events.clear(); }

int EventSim::run_queue(double arrive_rate, double service_rate, double sim_time) {
    // 单服务员 M/M/1 近似：到达间隔 Exp(1/arrive_rate)，服务时长 Exp(1/service_rate)
    clear();
    double t = 0;
    int done = 0;
    int busy_until = 0;      // 服务员下一次空闲时刻
    unsigned long long rng = 12345;
    // 简易随机数（线性同余，便于复现）
    auto rnd = [&rng]() {
        rng = rng * 1103515245 + 12345;
        return (double)((rng >> 16) & 0x7FFF) / 32767.0;
    };
    // 指数分布：-ln(1-u)/lambda
    auto expo = [&rnd](double rate) {
        double u = rnd();
        if (u < 1e-12) u = 1e-12;
        return -std::log(1.0 - u) / rate;
    };
    schedule(expo(arrive_rate), EV_ARRIVAL, 0);
    SimEvent ev;
    while (pop_next(ev)) {
        if (ev.time > sim_time) break;
        if (ev.kind == EV_ARRIVAL) {
            // 新顾客到达：安排下一位顾客
            schedule(ev.time + expo(arrive_rate), EV_ARRIVAL, 0);
            // 若服务员空闲则立即开始服务
            if (busy_until <= ev.time) {
                double serve = expo(service_rate);
                busy_until = (int)(ev.time + serve);
                schedule(ev.time + serve, EV_SERVICE, 1);
            }
        } else if (ev.kind == EV_SERVICE) {
            done++;
            // 队列中若有等待顾客（模拟简化：这里用 pending>0 近似）
            if (pending() > 0 && peek_time() >= ev.time) {
                double serve = expo(service_rate);
                busy_until = (int)(ev.time + serve);
                schedule(ev.time + serve, EV_SERVICE, 1);
            }
        }
    }
    return done;
}

// ---- self test ----
int EventSim::self_test() {
    int fails = 0;
    // 1. 基本插入与弹出（时间序）
    {
        EventSim s;
        s.schedule(3.0, EV_TIMER, 1);
        s.schedule(1.0, EV_TIMER, 2);
        s.schedule(2.0, EV_TIMER, 3);
        if (s.pending() != 3) fails++;
        SimEvent e;
        s.pop_next(e);
        if (e.time != 1.0 || e.data != 2) fails++;
        s.pop_next(e);
        if (e.time != 2.0 || e.data != 3) fails++;
        s.pop_next(e);
        if (e.time != 3.0 || e.data != 1) fails++;
        if (s.pending() != 0) fails++;
    }
    // 2. peek 与空表
    {
        EventSim s;
        if (s.peek_time() != -1) fails++;
        s.schedule(5.5, EV_CUSTOM, 9);
        if (s.peek_time() != 5.5) fails++;
        if (s.pending() != 1) fails++;
        s.clear();
        if (s.pending() != 0) fails++;
    }
    // 3. 相同时刻事件保持插入顺序（>= 使后插的在后面）
    {
        EventSim s;
        s.schedule(1.0, EV_TIMER, 1);
        s.schedule(1.0, EV_TIMER, 2);
        SimEvent e;
        s.pop_next(e);
        if (e.data != 1) fails++;
        s.pop_next(e);
        if (e.data != 2) fails++;
    }
    // 4. 排队模拟：服务速率高时应处理较多顾客
    {
        EventSim s;
        int done = s.run_queue(2.0, 10.0, 100.0);
        if (done < 5) fails++;   // 至少处理几个顾客
    }
    // 5. 零服务速率：几乎不完成
    {
        EventSim s;
        int done = s.run_queue(0.001, 0.0, 10.0);
        if (done != 0) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
