// nefuOS data-types library — timer wheel (timerwheel)
// 时间轮：环形刻度 + 定时器链表，用于海量定时器的高效调度
// （OS 定时器、网络超时、游戏技能 CD 的经典实现）。
// 教学版实现：单个双向逻辑链表 + 每 tick 统一递减毫秒；
// 到期定时器收集到 fired 数组。O(n) 每 tick（教学可接受），
// 接口与真实时间轮一致：add_timer / cancel / tick。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int TW_MAX_TIMERS = 512;

struct TimerEntry {
    int id;
    int ms_left;      // 剩余毫秒（<=0 到期）
    TimerEntry* next;
};

class timerwheel {
public:
    timerwheel() : head_(0), count_(0), next_id_(1) {}
    ~timerwheel() {
        TimerEntry* n = head_;
        while (n) { TimerEntry* nx = n->next; delete n; n = nx; }
    }
    timerwheel(const timerwheel&) = delete;
    timerwheel& operator=(const timerwheel&) = delete;

    // 注册一个 ms 毫秒后的定时器，返回定时器 id（-1 = 满）
    int add_timer(int ms) {
        if (count_ >= TW_MAX_TIMERS) return -1;
        TimerEntry* e = new TimerEntry;
        e->id = next_id_++;
        e->ms_left = ms > 0 ? ms : 0;
        e->next = head_;
        head_ = e;
        count_++;
        return e->id;
    }
    // 取消定时器；返回是否取消成功
    bool cancel(int id) {
        TimerEntry** p = &head_;
        while (*p) {
            if ((*p)->id == id) {
                TimerEntry* t = *p;
                *p = t->next;
                delete t;
                count_--;
                return true;
            }
            p = &(*p)->next;
        }
        return false;
    }
    // 推进一个 tick（1ms）；到期的定时器 id 写入 fired；返回触发数
    int tick(int* fired, int cap) {
        int n = 0;
        TimerEntry** p = &head_;
        while (*p) {
            TimerEntry* t = *p;
            t->ms_left--;
            if (t->ms_left <= 0) {
                if (n < cap) fired[n] = t->id;   // fired 装不下也照常计数
                n++;
                *p = t->next;
                delete t;
                count_--;
            } else {
                p = &(*p)->next;
            }
        }
        return n;
    }
    int timer_count() const { return count_; }

private:
    TimerEntry* head_;
    int count_;
    int next_id_;
};

int timerwheel_self_test();

} // namespace dt
} // namespace nefu
