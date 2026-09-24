// nefuOS 网络协议栈 —— 报文环形日志实现
#include "pktlog.h"
#include "netproto_common.h"
#include "ip.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

PktLog::PktLog() : ring_(0), cap_(0), head_(0), count_(0) {}

void PktLog::init(int capacity) {
    if (ring_) delete[] ring_;
    if (capacity < 1) capacity = 1;
    ring_ = new PktLogEntry[capacity];
    cap_ = capacity;
    head_ = 0;
    count_ = 0;
}

void PktLog::log(uint8_t dir, uint8_t proto, uint16_t len,
                 uint32_t src, uint32_t dst, uint32_t now_ms) {
    if (!ring_) return;
    PktLogEntry& e = ring_[head_];
    e.tick_ms = now_ms;
    e.dir = dir;
    e.proto = proto;
    e.len = len;
    e.src = src;
    e.dst = dst;
    head_ = (head_ + 1) % cap_;
    if (count_ < cap_) count_++;
}

int PktLog::size() const { return count_; }

const PktLogEntry* PktLog::at(int i) const {
    if (i < 0 || i >= count_ || !ring_) return 0;
    // i=0 最新
    int idx = (head_ - 1 - i + cap_) % cap_;
    return &ring_[idx];
}

void PktLog::clear() { head_ = 0; count_ = 0; }

void PktLog::format(int i, char* buf, int bufsz) const {
    if (bufsz > 0) buf[0] = 0;
    const PktLogEntry* e = at(i);
    if (!e) return;
    char sb[16], db[16];
    ip_format(e->src, sb, sizeof(sb));
    ip_format(e->dst, db, sizeof(db));
    snprintf(buf, (size_t)bufsz, "%s %-4s %5uB  %s -> %s",
             e->dir ? "TX" : "RX",
             ip_proto_name(e->proto),
             (unsigned)e->len, sb, db);
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [pktlog] FAIL: %s\n", what); }
}
} // namespace

int pktlog_self_test() {
    g_fails = 0;
    PktLog lg;
    lg.init(4);
    uint32_t a = (192u<<24)|(168u<<16)|1u;
    lg.log(0, IPPROTO_ICMP, 64, a, 0x08080808u, 1000);
    lg.log(1, IPPROTO_TCP,  40, a, 0x0A000001u, 1001);
    lg.log(0, IPPROTO_UDP,  12, a, 0x08080808u, 1002);
    expect("pktlog size 3", lg.size() == 3);
    expect("pktlog newest is udp", lg.at(0)->proto == IPPROTO_UDP);
    expect("pktlog oldest is icmp", lg.at(2)->proto == IPPROTO_ICMP);

    // 环形覆盖：再写两条，旧的 ICMP 应被挤掉
    lg.log(1, IPPROTO_TCP, 55, a, 0x0A000002u, 1003);
    lg.log(0, IPPROTO_TCP, 60, a, 0x0A000003u, 1004);
    expect("pktlog wraps to 4", lg.size() == 4);
    expect("pktlog newest", lg.at(0)->len == 60);
    expect("pktlog oldest now tcp", lg.at(3)->proto == IPPROTO_TCP);

    char buf[96];
    lg.format(0, buf, sizeof(buf));
    expect("pktlog format", strstr(buf, "TCP") != 0);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
