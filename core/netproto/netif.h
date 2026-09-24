// nefuOS 网络协议栈 —— 网络接口抽象
// 一个"网卡"实例：持有 MAC/IPv4/掩码/网关，关联 ARP 缓存与统计计数器，
// 并提供"收到一个 IP 包该往哪送"的路由决策。本模块不做真收发，
// 只把上面各子模块（ethernet/arp/ip）串成一个可用的接口对象。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"

namespace nefu {
namespace netproto {

struct NetIf {
    MacAddr  mac;
    uint32_t ip;          // 主机序
    uint32_t netmask;
    uint32_t gateway;
    char     name[8];     // "eth0"
    EthStats stats;
    ArpCache arp;

    NetIf();
    void reset();
    // 配置 IP（CIDR 风格：ip + 掩码）
    void set_ip(uint32_t addr, uint32_t mask);
    // 判断 addr 是否在本接口的直连子网内
    bool on_link(uint32_t addr) const;
    // 决策去 dst 的下一跳 IP：直连返回 dst，跨网返回网关；无路由返回 0。
    uint32_t next_hop(uint32_t dst, const RouteTable& rt) const;
    void format_status(char* buf, int bufsz) const;
};

int netif_self_test();

} // namespace netproto
} // namespace nefu
