// nefuOS 网络协议栈 —— 以太网实现
#include "ethernet.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ===================== MacAddr =====================
bool MacAddr::operator==(const MacAddr& o) const {
    for (int i = 0; i < 6; i++) if (oct[i] != o.oct[i]) return false;
    return true;
}

bool MacAddr::is_zero() const {
    for (int i = 0; i < 6; i++) if (oct[i] != 0) return false;
    return true;
}

bool MacAddr::is_broadcast() const {
    for (int i = 0; i < 6; i++) if (oct[i] != 0xFF) return false;
    return true;
}

// 组播/广播位：首字节最低位（I/G bit）。1 = 组播或广播。
bool MacAddr::is_multicast() const {
    return (oct[0] & 0x01u) != 0;
}

// 本地管理位：首字节次低位（U/L bit）。1 = 本地分配（如随机 MAC）。
bool MacAddr::is_local_admin() const {
    return (oct[0] & 0x02u) != 0;
}

// 把一个十六进制字符转成 0..15，非法返回 -1。
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool mac_parse(const char* s, MacAddr& out) {
    if (!s) return false;
    // 期望 6 组两位十六进制，分隔符为 ':' 或 '-'
    for (int i = 0; i < 6; i++) {
        int hi = hex_nibble(s[0]);
        int lo = hex_nibble(s[1]);
        if (hi < 0 || lo < 0) return false;
        out.oct[i] = (uint8_t)((hi << 4) | lo);
        s += 2;
        if (i < 5) {
            if (*s != ':' && *s != '-') return false;
            s++;   // 跳过分隔符
        }
    }
    return true;
}

char* mac_format(const MacAddr& m, char* buf, int bufsz) {
    static const char* H = "0123456789ABCDEF";
    if (bufsz < 18) { if (buf && bufsz > 0) buf[0] = 0; return buf; }
    int o = 0;
    for (int i = 0; i < 6; i++) {
        buf[o++] = H[(m.oct[i] >> 4) & 0xF];
        buf[o++] = H[m.oct[i] & 0xF];
        if (i < 5) buf[o++] = ':';
    }
    buf[o] = 0;
    return buf;
}

MacAddr mac_zero() {
    MacAddr m;
    np_zero(m.oct, 6);
    return m;
}

MacAddr mac_broadcast() {
    MacAddr m;
    for (int i = 0; i < 6; i++) m.oct[i] = 0xFF;
    return m;
}

bool mac_is_broadcast(const MacAddr& m) {
    for (int i = 0; i < 6; i++) if (m.oct[i] != 0xFF) return false;
    return true;
}
bool mac_is_multicast(const MacAddr& m) {
    return (m.oct[0] & 1) != 0;
}

// ===================== 帧封装 / 解析 =====================
int eth_pack_header(uint8_t* out, const MacAddr& dst, const MacAddr& src, uint16_t ethertype) {
    if (!out) return -1;
    for (int i = 0; i < 6; i++) out[i] = dst.oct[i];
    for (int i = 0; i < 6; i++) out[6 + i] = src.oct[i];
    put_be16(out + 12, ethertype);
    return ETH_HDR_LEN;
}

int eth_parse_header(const uint8_t* pkt, int len, EthHeader& h) {
    if (!pkt || len < ETH_HDR_LEN) return -1;
    for (int i = 0; i < 6; i++) h.dst.oct[i] = pkt[i];
    for (int i = 0; i < 6; i++) h.src.oct[i] = pkt[6 + i];
    h.ethertype = be16(pkt + 12);
    return ETH_HDR_LEN;
}

int eth_pad_needed(int payload_len) {
    if (payload_len >= ETH_MIN_PAYLOAD) return 0;
    return ETH_MIN_PAYLOAD - payload_len;
}

const char* ethertype_name(uint16_t et) {
    switch (et) {
    case ET_IPV4: return "IPv4";
    case ET_ARP:  return "ARP";
    case ET_VLAN: return "802.1Q";
    case ET_IPV6: return "IPv6";
    default:      return "UNKNOWN";
    }
}

int eth_pack_frame(uint8_t* out, const MacAddr& dst, const MacAddr& src,
                  uint16_t ethertype, const uint8_t* payload, int payload_len) {
    if (!out) return -1;
    eth_pack_header(out, dst, src, ethertype);
    int o = ETH_HDR_LEN;
    if (payload && payload_len > 0) {
        np_copy(out + o, payload, payload_len);
        o += payload_len;
    }
    // 零填充到最小帧长 60 字节
    while (o < ETH_MIN_FRAME) out[o++] = 0;
    return o;
}

int eth_pack_vlan_tag(uint8_t* out_after_src, uint16_t tci, uint16_t inner_ethertype) {
    if (!out_after_src) return -1;
    put_be16(out_after_src + 0, ET_VLAN);       // TPID = 0x8100
    put_be16(out_after_src + 2, tci);           // TCI (PCP+DEI+VID)
    put_be16(out_after_src + 4, inner_ethertype);
    return 6;                                   // 标签 4 + 内层类型 2 = 6 字节
}

bool eth_is_vlan(const EthHeader& h) { return h.ethertype == ET_VLAN; }

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [eth] FAIL: %s\n", what); }
}
} // namespace

int ethernet_self_test() {
    g_fails = 0;

    // 1) MAC 解析 / 格式化往返
    MacAddr m;
    bool ok = mac_parse("AA:BB:CC:DD:EE:FF", m);
    expect("mac_parse colon", ok);
    expect("mac_parse byte0", m.oct[0] == 0xAA);
    expect("mac_parse byte5", m.oct[5] == 0xFF);

    char mb[24];
    mac_format(m, mb, sizeof(mb));
    expect("mac_format roundtrip", strcmp(mb, "AA:BB:CC:DD:EE:FF") == 0);

    // 短横线分隔
    MacAddr m2;
    ok = mac_parse("00-1A-2B-3C-4D-5E", m2);
    expect("mac_parse dash", ok && m2.oct[0] == 0x00 && m2.oct[5] == 0x5E);

    // 非法字符串
    MacAddr bad;
    expect("mac_parse garbage", !mac_parse("not-a-mac", bad));
    expect("mac_parse short", !mac_parse("AA:BB:CC", bad));

    // 2) 广播 / 组播 / 单播 判定
    MacAddr bcast = mac_broadcast();
    expect("broadcast detect", bcast.is_broadcast() && bcast.is_multicast());
    MacAddr zero = mac_zero();
    expect("zero detect", zero.is_zero());

    // 组播 MAC：首字节最低位为 1，例如 01:00:5E:xx:xx:xx
    MacAddr mcast;
    mac_parse("01:00:5E:00:00:01", mcast);
    expect("multicast detect", mcast.is_multicast() && !mcast.is_broadcast());

    // 单播本地管理 MAC（常见随机 MAC，首字节第二位为 1）：02:xx:xx:xx:xx:xx
    MacAddr local;
    mac_parse("02:AB:CD:EF:01:23", local);
    expect("local-admin detect", local.is_local_admin() && local.is_unicast());

    // 3) 帧封装字节序列已知向量
    //    dst=FF:FF:FF:FF:FF:FF  src=00:11:22:33:44:55  type=0x0806(ARP)
    uint8_t frame[ETH_HDR_LEN];
    MacAddr src; mac_parse("00:11:22:33:44:55", src);
    int n = eth_pack_header(frame, bcast, src, ET_ARP);
    expect("eth_pack len", n == ETH_HDR_LEN);
    expect("eth_pack dst0", frame[0] == 0xFF && frame[5] == 0xFF);
    expect("eth_pack src3", frame[8] == 0x22);
    expect("eth_pack type", frame[12] == 0x08 && frame[13] == 0x06);

    // 4) 解析回读
    EthHeader h;
    int r = eth_parse_header(frame, sizeof(frame), h);
    expect("eth_parse ok", r == ETH_HDR_LEN);
    expect("eth_parse dst bcast", h.dst == bcast);
    expect("eth_parse src", h.src == src);
    expect("eth_parse type", h.ethertype == ET_ARP);

    // 5) 太短应拒绝
    expect("eth_parse too short", eth_parse_header(frame, 10, h) == -1);

    // 6) 填充计算
    expect("pad 0 for 100", eth_pad_needed(100) == 0);
    expect("pad 26 for 20", eth_pad_needed(20) == ETH_MIN_PAYLOAD - 20);
    expect("pad for 0", eth_pad_needed(0) == ETH_MIN_PAYLOAD);

    // 7) 名字
    expect("name ipv4", strcmp(ethertype_name(ET_IPV4), "IPv4") == 0);
    expect("name arp", strcmp(ethertype_name(ET_ARP), "ARP") == 0);

    // 8) 完整帧封装：短 payload 自动填充到 60
    uint8_t bigframe[128];
    const uint8_t smallpayload[10] = {1,2,3,4,5,6,7,8,9,10};
    n = eth_pack_frame(bigframe, bcast, src, ET_IPV4, smallpayload, 10);
    expect("frame min 60", n == ETH_MIN_FRAME);
    // 尾部填充应为 0
    bool padzero = true;
    for (int i = ETH_HDR_LEN + 10; i < ETH_MIN_FRAME; i++)
        if (bigframe[i] != 0) padzero = false;
    expect("frame pad zero", padzero);
    // 长 payload 不填充
    uint8_t bigpayload[100];
    for (int i = 0; i < 100; i++) bigpayload[i] = (uint8_t)i;
    n = eth_pack_frame(bigframe, bcast, src, ET_IPV4, bigpayload, 100);
    expect("frame long no pad", n == ETH_HDR_LEN + 100);

    // 9) VLAN 标签：dst(6)+src(6)+TPID(2)+TCI(2)+inner_type(2) = 18 字节头
    uint8_t vbuf[32];
    MacAddr d; mac_parse("00:0c:29:aa:bb:cc", d);
    int vn = eth_pack_vlan_tag(vbuf, 100, ET_IPV4);
    expect("vlan hdr len", vn == 6);
    expect("vlan tpid", be16(vbuf) == 0x8100);
    expect("vlan tci vid", be16(vbuf+2) == 100);
    expect("vlan inner type", be16(vbuf+4) == ET_IPV4);
    EthHeader vh;
    vh.ethertype = ET_VLAN;
    expect("vlan detect", eth_is_vlan(vh));


    expect("mac broadcast", mac_is_broadcast(mac_broadcast()));
    expect("mac mcast", mac_is_multicast(MacAddr{{0x01,0x00,0x5E,0,0,1}}));
    return g_fails;
}

} // namespace netproto
} // namespace nefu
