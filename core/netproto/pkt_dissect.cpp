// nefuOS 网络协议栈 —— 报文解剖器实现
#include "pkt_dissect.h"
#include "netproto_common.h"
#include "ethernet.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

int pkt_dissect_ip(const uint8_t* ip_pkt, int len, char* out, int outsz) {
    if (!out || outsz < 1) return 0;
    out[0] = 0;
    if (!ip_pkt || len < 20) { snprintf(out, (size_t)outsz, "bad ip"); return (int)strlen(out); }
    IpHeader h;
    int hlen = ip_parse_header(ip_pkt, len, h, true);
    if (hlen < 0) { snprintf(out, (size_t)outsz, "ip csum bad"); return (int)strlen(out); }

    char sb[16], db[16];
    ip_format(h.src, sb, sizeof(sb));
    ip_format(h.dst, db, sizeof(db));

    const uint8_t* l4 = ip_pkt + hlen;
    int l4len = len - hlen;

    if (h.protocol == IPPROTO_TCP && l4len >= 20) {
        TcpHeader t;
        if (tcp_parse(l4, l4len, t) >= 0) {
            char fl[16]; tcp_flags_str(t.flags, fl, sizeof(fl));
            snprintf(out, (size_t)outsz, "TCP %s:%u > %s:%u [%s] seq=%u ack=%u win=%u",
                     sb, (unsigned)t.src_port, db, (unsigned)t.dst_port,
                     fl, (unsigned)t.seq, (unsigned)t.ack, (unsigned)t.window);
        }
    } else if (h.protocol == IPPROTO_UDP && l4len >= 8) {
        uint16_t sport = be16(l4), dport = be16(l4 + 2), ulen = be16(l4 + 4);
        snprintf(out, (size_t)outsz, "UDP %s:%u > %s:%u len=%u",
                 sb, (unsigned)sport, db, (unsigned)dport, (unsigned)ulen);
    } else if (h.protocol == IPPROTO_ICMP && l4len >= 2) {
        snprintf(out, (size_t)outsz, "ICMP %s %s -> %s",
                 icmp_type_name(l4[0]), sb, db);
    } else {
        snprintf(out, (size_t)outsz, "%s %s > %s len=%d",
                 ip_proto_name(h.protocol), sb, db, len);
    }
    return (int)strlen(out);
}

int pkt_dissect_ethernet(const uint8_t* frame, int len, char* out, int outsz) {
    if (!out || outsz < 1) return 0;
    out[0] = 0;
    if (!frame || len < 14) { snprintf(out, (size_t)outsz, "bad eth"); return (int)strlen(out); }
    EthHeader e;
    int n = eth_parse_header(frame, len, e);
    if (n < 0) { snprintf(out, (size_t)outsz, "eth parse fail"); return (int)strlen(out); }

    char dbuf[18], sbuf[18];
    mac_format(e.dst, dbuf, sizeof(dbuf));
    mac_format(e.src, sbuf, sizeof(sbuf));

    if (e.ethertype == 0x0800) {
        // IPv4
        pkt_dissect_ip(frame + n, len - n, out, outsz);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "eth %s > %s | ", sbuf, dbuf);
        char tmp[256]; snprintf(tmp, sizeof(tmp), "%s%s", prefix, out);
        snprintf(out, (size_t)outsz, "%s", tmp);
    } else if (e.ethertype == 0x0806) {
        snprintf(out, (size_t)outsz, "ARP %s > %s", sbuf, dbuf);
    } else {
        snprintf(out, (size_t)outsz, "eth %s > %s type=0x%04X",
                 sbuf, dbuf, (unsigned)e.ethertype);
    }
    return (int)strlen(out);
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [dissect] FAIL: %s\n", what); }
}
} // namespace

int pkt_dissect_self_test() {
    g_fails = 0;
    // 构造一个 IP/UDP 包再解剖
    uint8_t pkt[64];
    uint32_t me = (192u<<24)|(168u<<16)|(1u<<8)|10u;
    uint32_t dns= (8u<<24)|(8u<<16)|(8u<<8)|8u;
    ip_pack_header(pkt, 1, 64, IPPROTO_UDP, me, dns, 48, 0);
    // UDP 头（含伪首部校验和）
    uint8_t udp_payload[8] = {0x12,0x34,0x01,0x00,0x00,0x01,0x00,0x00};
    int n = udp_pack(pkt + 20, 53, 12345, udp_payload, 8, me, dns);
    (void)n;

    char desc[256];
    pkt_dissect_ip(pkt, 48, desc, sizeof(desc));
    expect("dissect udp", strstr(desc, "UDP") != 0 && strstr(desc, "8.8.8.8") != 0);

    // 构造一个以太网帧包裹它
    uint8_t frame[128];
    MacAddr dst = {{0x00,0x11,0x22,0x33,0x44,0x55}};
    MacAddr src = {{0x52,0x54,0x00,0xAB,0xCD,0xEF}};
    eth_pack_frame(frame, dst, src, 0x0800, pkt, 48);
    int flen = n + 14;
    (void)flen;
    pkt_dissect_ethernet(frame, 14 + 48, desc, sizeof(desc));
    expect("dissect eth", strstr(desc, "UDP") != 0 && strstr(desc, "eth") != 0);

    // 坏包
    pkt_dissect_ip(0, 0, desc, sizeof(desc));
    expect("dissect bad", strlen(desc) > 0);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
