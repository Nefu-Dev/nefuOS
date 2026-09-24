// nefuOS simlib —— 离散事件模拟器 queue
// 教学版：按时间轴处理事件的队列（事件 = 触发时刻 + 回调 + 用户数据）。
// 经典用途：排队系统、任务调度、网络包模拟。class / STL / 中文注释。
#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace nefu {
namespace simx {

// 事件类型枚举（用户可扩展，教学示例内置几种）
enum EventKind {
    EV_NONE = 0,
    EV_ARRIVAL,   // 顾客到达
    EV_SERVICE,   // 服务完成
    EV_TIMER,     // 定时器
    EV_CUSTOM     // 自定义
};

// 单个事件：时刻 + 种类 + 附带数值
struct SimEvent {
    double    time;    // 触发时刻
    int       kind;    // EventKind
    int       data;    // 附带整数
    // 构造
    SimEvent() : time(0), kind(EV_NONE), data(0) {}
    SimEvent(double t, int k, int d) : time(t), kind(k), data(d) {}
};

// 离散事件模拟器（最小堆式事件表）
class EventSim {
public:
    EventSim();

    // 在时刻 t 安排一个事件（按时间排序插入）
    void schedule(double t, int kind, int data);
    // 取出下一个最早事件（pop；无事件返回 false）
    bool pop_next(SimEvent& ev);
    // 查看下一个事件时刻（无事件返回 -1）
    double peek_time() const;
    // 清空事件表
    void clear();
    // 当前待处理事件数
    int pending() const { return (int)events.size(); }

    // 简单"服务员"模型：顾客到达 -> 服务时长 -> 完成
    // 返回处理完的顾客数
    int run_queue(double arrive_rate, double service_rate, double sim_time);

    // ---- self test ----
    static int self_test();

private:
    // 最小堆：按 time 排序（教学版直接线性插入，事件数不大时足够）
    std::vector<SimEvent> events;
};

} // namespace simx
} // namespace nefu
