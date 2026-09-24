// nefuOS 网络协议栈 —— 顶层协议栈对象实现
#include "netstack.h"
#include "netproto_common.h"
#include "icmp.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

NetStack::NetStack() { init(); }

void NetStack::init() {
    nif.reset();
    routes.clear();
    log.init(8);
    udp.clear();
    tcp.clear();
    rx_bytes = tx_bytes = 0;
    rx_packets = tx_packets = 0;
    // 默认路由
    routes.add_cidr("0.0.0.0/0", 0xC0A80101u, "eth0");
    routes.add_cidr("192.168.1.0/24", 0, "eth0");
}

int NetStack::on_ip_packet(const uint8_t* ip_pkt, int len, uint32_t now_ms) {
    IpHeader h;
    int hlen = ip_parse_header(ip_pkt, len, h, true);
    if (hlen < 0) return 0;       // 校验和错，丢
    log.log(0, h.protocol, (uint16_t)len, h.src, h.dst, now_ms);
    rx_bytes += (uint64_t)len;
    rx_packets++;
    // 按协议号上交（这里只做分类，不真的解析传输层）
    if (h.protocol == IPPROTO_ICMP || h.protocol == IPPROTO_TCP ||
        h.protocol == IPPROTO_UDP) return 1;
    return 0;
}

void NetStack::on_ip_outgoing(int len) {
    tx_bytes += (uint64_t)len;
    tx_packets++;
}

uint32_t NetStack::route_outgoing(uint32_t dst) const {
    RouteEntry e;
    if (!routes.lookup(dst, e)) return 0;
    if (e.gateway == 0) return dst;       // 直连
    return e.gateway;
}

void NetStack::summary(char* buf, int bufsz) {
    snprintf(buf, (size_t)bufsz,
             "if=%s routes=%d udp=%d tcp=%d rx=%llu/%llu tx=%llu/%llu",
             nif.name, routes.size(), udp.size(), tcp.size(),
             (unsigned long long)rx_packets, (unsigned long long)rx_bytes,
             (unsigned long long)tx_packets, (unsigned long long)tx_bytes);
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [netstack] FAIL: %s\n", what); }
}
} // namespace

int netstack_self_test() {
    g_fails = 0;
    NetStack st;
    expect("stack routes", st.routes.size() == 2);

    // 构造一个 ICMP IP 包喂进去
    uint8_t ip[40];
    uint32_t me = (192u<<24)|(168u<<16)|(1u<<8)|10u;
    uint32_t gw = (192u<<24)|(168u<<16)|(1u<<8)|1u;
    ip_pack_header(ip, 1, 64, IPPROTO_ICMP, gw, me, 40, 0);
    int act = st.on_ip_packet(ip, 40, 1000);
    expect("stack accepts icmp", act == 1);
    expect("stack logged", st.log.size() == 1);
    st.on_ip_outgoing(60);
    expect("stack tx stats", st.tx_packets == 1 && st.tx_bytes == 60);
    expect("stack rx stats", st.rx_packets == 1);
    // 出站路由：8.8.8.8 应走默认网关
    uint32_t nh = st.route_outgoing(0x08080808u);
    expect("stack route via gw", nh == 0xC0A80101u);
    uint32_t local = st.route_outgoing(0xC0A80150u);
    expect("stack route direct", local == 0xC0A80150u);

    // 校验和错的包应被丢
    ip[10] ^= 0xFF;
    expect("stack drops bad csum", st.on_ip_packet(ip, 40, 1001) == 0);

    char buf[96];
    st.summary(buf, sizeof(buf));
    expect("stack summary", strstr(buf, "routes=2") != 0);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
