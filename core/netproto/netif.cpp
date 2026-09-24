// nefuOS 网络协议栈 —— 网络接口抽象实现
#include "netif.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

NetIf::NetIf() {
    reset();
}

void NetIf::reset() {
    mac = mac_zero();
    ip = 0; netmask = 0; gateway = 0;
    np_zero(name, sizeof(name));
    name[0] = 'e'; name[1] = 't'; name[2] = 'h'; name[3] = '0'; name[4] = 0;
    stats.reset();
    arp.clear();
}

void NetIf::set_ip(uint32_t addr, uint32_t mask) {
    ip = addr;
    netmask = mask;
    // 网关默认取子网第一个可用地址（x.x.x.1）
    gateway = (addr & mask) | 1u;
}

bool NetIf::on_link(uint32_t addr) const {
    return (addr & netmask) == (ip & netmask);
}

uint32_t NetIf::next_hop(uint32_t dst, const RouteTable& rt) const {
    RouteEntry e;
    if (!rt.lookup(dst, e)) return 0;
    if (e.gateway == 0) return dst;     // 直连
    return e.gateway;
}

void NetIf::format_status(char* buf, int bufsz) const {
    char ipb[20], maskb[20], gwb[20], macb[20];
    ip_format(ip, ipb, sizeof(ipb));
    ip_format(netmask, maskb, sizeof(maskb));
    ip_format(gateway, gwb, sizeof(gwb));
    mac_format(mac, macb, sizeof(macb));
    snprintf(buf, (size_t)bufsz,
             "%s  %s  ip=%s mask=%s gw=%s  rx=%u tx=%u",
             name, macb, ipb, maskb, gwb, stats.rx_frames, stats.tx_frames);
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [netif] FAIL: %s\n", what); }
}
} // namespace

int netif_self_test() {
    g_fails = 0;
    NetIf nif;
    MacAddr m; mac_parse("52:54:00:12:34:56", m);
    nif.mac = m;
    nif.set_ip((192u<<24)|(168u<<16)|(1u<<8)|10u, 0xFFFFFF00u);

    expect("netif name", strcmp(nif.name, "eth0") == 0);
    expect("netif on-link local", nif.on_link((192u<<24)|(168u<<16)|(1u<<8)|50u));
    expect("netif off-link", !nif.on_link((8u<<24)|(8u<<16)|(8u<<8)|8u));
    expect("netif gateway", nif.gateway == (192u<<24)|(168u<<16)|(1u<<8)|1u);

    // 路由决策
    RouteTable rt;
    rt.add_cidr("0.0.0.0/0", nif.gateway, "eth0");
    rt.add_cidr("192.168.1.0/24", 0, "eth0");
    expect("nexthop direct", nif.next_hop((192u<<24)|(168u<<16)|(1u<<8)|50u, rt) ==
           (192u<<24)|(168u<<16)|(1u<<8)|50u);
    expect("nexthop via gw", nif.next_hop((8u<<24)|(8u<<16)|(8u<<8)|8u, rt) ==
           nif.gateway);

    // 格式化不崩
    char buf[128];
    nif.format_status(buf, sizeof(buf));
    expect("format has ip", strstr(buf, "192.168.1.10") != 0);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
