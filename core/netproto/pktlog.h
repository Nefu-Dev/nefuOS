// nefuOS 网络协议栈 —— 报文环形日志
// 一个固定容量的环形缓冲，记录最近 N 条收发报文的摘要（方向/协议/长度/时间戳）。
// 用于 netlab 窗口展示"最近在跑什么包"。不持有完整报文，只存摘要，无堆分配。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

struct PktLogEntry {
    uint32_t tick_ms;      // 记录时刻
    uint8_t  dir;          // 0=rx 1=tx
    uint8_t  proto;        // IPPROTO_* 或以太类型
    uint16_t len;          // 报文长度
    uint32_t src;          // 主机序 IP
    uint32_t dst;
};

class PktLog {
public:
    PktLog();
    // 容量由构造时固定（建议 16~64）。
    void init(int capacity);

    void log(uint8_t dir, uint8_t proto, uint16_t len,
             uint32_t src, uint32_t dst, uint32_t now_ms);

    int  size() const;                 // 已记录条数（<=capacity）
    // 第 i 条（0=最新）。越界返回 0。
    const PktLogEntry* at(int i) const;
    void clear();
    // 把第 i 条格式化成一行文本（netlab 用）。
    void format(int i, char* buf, int bufsz) const;

private:
    PktLogEntry* ring_;
    int cap_;
    int head_;       // 下一个写入位置
    int count_;
};

int pktlog_self_test();

} // namespace netproto
} // namespace nefu
