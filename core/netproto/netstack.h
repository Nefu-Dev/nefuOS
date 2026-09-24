// nefuOS 网络协议栈 —— 顶层协议栈对象
// 把前面各子模块聚合成一个可驱动的协议栈：
//   NetIf（本端地址）+ RouteTable + PktLog + UdpTable + TcpTable。
// 它不真正收发字节，而是对外提供"喂入一个 IP 包 -> 决定下一步动作"的逻辑，
// 以及把各子模块状态汇总成一行文本（netlab 用）。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"
#include "netif.h"
#include "pktlog.h"
#include "udp.h"
#include "tcp.h"
#include "ip.h"

namespace nefu {
namespace netproto {

struct NetStack {
    NetIf     nif;
    RouteTable routes;
    PktLog    log;
    UdpTable  udp;
    TcpTable  tcp;

    // 字节统计
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;

    NetStack();
    void init();

    // 收一个入站 IP 包：填日志，按协议号分类。返回处理动作（1=上交传输层）。
    int on_ip_packet(const uint8_t* ip_pkt, int len, uint32_t now_ms);
    // 发一个出站 IP 包：记 tx 统计。
    void on_ip_outgoing(int len);

    // 出站路由：给 dst 决定下一跳（直连返回 dst，跨网返回网关）。
    uint32_t route_outgoing(uint32_t dst) const;

    // 汇总状态到 buf（一行）。
    void summary(char* buf, int bufsz);
};

int netstack_self_test();

} // namespace netproto
} // namespace nefu
