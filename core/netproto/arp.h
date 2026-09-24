// nefuOS 网络协议栈 —— ARP（地址解析协议，RFC 826）
//
// 作用：把 IPv4 地址（如 192.168.1.1）解析成对应的以太网 MAC 地址。
// 报文结构（以太网 payload，紧跟在 14 字节以太网头之后）：
//
//   硬件类型(2) 协议类型(2) 硬件长(1) 协议长(1) 操作码(2)
//   发送端MAC(6) 发送端IP(4) 目标MAC(6) 目标IP(4)
//
//   硬件类型=1(以太网)，协议类型=0x0800(IPv4)，硬件长=6，协议长=4，
//   操作码=1(请求)/2(应答)。整段固定 28 字节。
//
// 本模块同时维护一张 ARP 缓存：IP -> MAC，带超时淘汰。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"
#include "ethernet.h"

namespace nefu {
namespace netproto {

const int ARP_HDR_LEN = 28;

const uint16_t ARP_OP_REQUEST = 1;
const uint16_t ARP_OP_REPLY   = 2;

struct ArpPacket {
    uint16_t htype;       // 硬件类型
    uint16_t ptype;       // 协议类型
    uint8_t  hlen;        // 硬件地址长度
    uint8_t  plen;        // 协议地址长度
    uint16_t oper;        // 操作码
    MacAddr  sha;         // 发送端 MAC
    uint32_t spa;         // 发送端 IP（主机序）
    MacAddr  tha;         // 目标 MAC
    uint32_t tpa;         // 目标 IP（主机序）
};

// 封装一个 ARP 请求（"谁拥有 tpa？请告诉 spa"）。
// out 至少 28 字节，返回写入长度。
int arp_build_request(uint8_t* out, uint32_t sender_ip, const MacAddr& sender_mac,
                      uint32_t target_ip);

// 封装一个 ARP 应答。
int arp_build_reply(uint8_t* out, const MacAddr& sender_mac, uint32_t sender_ip,
                    const MacAddr& target_mac, uint32_t target_ip);

// 封装一个免费 ARP（gratuitous ARP）：宣告"我的 IP 是 my_ip，MAC 是 my_mac"。
int arp_build_gratuitous(uint8_t* out, const MacAddr& my_mac, uint32_t my_ip);

// 解析 ARP 报文。成功返回 0，长度非法返回 -1。
int arp_parse(const uint8_t* pkt, int len, ArpPacket& out);
// 操作码 -> 名字（"Request"/"Reply"）
const char* arp_op_name(uint16_t op);

// ===================== ARP 缓存 =====================
// 一条缓存项
struct ArpEntry {
    uint32_t ip;          // 主机序 IPv4
    MacAddr  mac;
    uint32_t expire_ms;  // 绝对过期时刻（platform_tick_ms 坐标系）
    bool     stale;       // 已确认过期（未删除，便于调试）
};

class ArpCache {
public:
    ArpCache() : entries_() {}

    // 插入或刷新一条映射。ttl_ms 为存活毫秒数（0 表示用默认 30s）。
    void add(uint32_t ip, const MacAddr& mac, uint32_t now_ms, uint32_t ttl_ms = 30000);

    // 查找 IP 对应的 MAC。找到且未过期返回 true 并写入 out_mac。
    bool lookup(uint32_t ip, MacAddr& out_mac, uint32_t now_ms);

    // 淘汰所有已过期项，返回剩余条目数。
    int  expire(uint32_t now_ms);

    int  size() const { return entries_.size(); }
    const ArpEntry& at(int i) const { return entries_[i]; }
    void clear() { entries_.clear(); }

    // 把缓存里第 i 项格式化成 "192.168.1.1 -> AA:BB:..." 文本（调试/netlab 用）。
    void format_entry(int i, char* buf, int bufsz) const;

private:
    List<ArpEntry> entries_;
};

int arp_self_test();

} // namespace netproto
} // namespace nefu
