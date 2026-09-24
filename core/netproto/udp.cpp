// nefuOS 网络协议栈 —— UDP 实现
#include "udp.h"
#include "netproto_common.h"
#include "ip.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

int udp_pack(uint8_t* out, uint16_t src_port, uint16_t dst_port,
             const uint8_t* payload, int payload_len,
             uint32_t src_ip, uint32_t dst_ip) {
    if (!out) return -1;
    int total = UDP_HDR_LEN + payload_len;
    put_be16(out + 0, src_port);
    put_be16(out + 2, dst_port);
    put_be16(out + 4, (uint16_t)total);
    put_be16(out + 6, 0);                 // 校验和先置 0
    if (payload && payload_len > 0) np_copy(out + UDP_HDR_LEN, payload, payload_len);
    uint16_t c = transport_csum(out, total, src_ip, dst_ip, IPPROTO_UDP);
    // IPv4 下 UDP 校验和可省；但若算出来是 0，要写成 0xFFFF（0 表示"无校验和"）
    if (c == 0) c = 0xFFFFu;
    put_be16(out + 6, c);
    return total;
}

bool udp_verify_checksum(const uint8_t* seg, int seg_len,
                         uint32_t src_ip, uint32_t dst_ip) {
    if (!seg || seg_len < UDP_HDR_LEN) return false;
    uint16_t stored = be16(seg + 6);
    if (stored == 0) return true;   // IPv4 允许不校验
    return transport_csum(seg, seg_len, src_ip, dst_ip, IPPROTO_UDP) == 0;
}

int udp_parse(const uint8_t* seg, int len, uint32_t src_ip, uint32_t dst_ip,
              UdpPacket& out, bool verify_csum) {
    if (!seg || len < UDP_HDR_LEN) return -1;
    out.hdr.src_port = be16(seg + 0);
    out.hdr.dst_port = be16(seg + 2);
    out.hdr.length   = be16(seg + 4);
    uint16_t csum    = be16(seg + 6);
    if (out.hdr.length < UDP_HDR_LEN || out.hdr.length > len) return -1;
    out.data     = seg + UDP_HDR_LEN;
    out.data_len = out.hdr.length - UDP_HDR_LEN;

    if (verify_csum && csum != 0) {
        uint16_t c = transport_csum(seg, out.hdr.length, src_ip, dst_ip, IPPROTO_UDP);
        if (c != 0) return -2;            // 校验和错误
    }
    return 0;
}

// ===================== UDP 端口表 =====================
bool UdpTable::bind(uint16_t port) {
    for (int i = 0; i < eps_.size(); i++)
        if (eps_[i].port == port && eps_[i].bound) return false;
    UdpEndpoint e; e.port = port; e.bound = true;
    eps_.push(e);
    return true;
}

bool UdpTable::is_bound(uint16_t port) const {
    for (int i = 0; i < eps_.size(); i++)
        if (eps_[i].port == port && eps_[i].bound) return true;
    return false;
}

void UdpTable::close(uint16_t port) {
    for (int i = 0; i < eps_.size(); i++)
        if (eps_[i].port == port) { eps_[i].bound = false; return; }
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [udp] FAIL: %s\n", what); }
}
} // namespace

int udp_self_test() {
    g_fails = 0;
    uint8_t buf[256];

    uint32_t src = 0xC0A8010Au, dst = 0xD043DEDEu;  // 192.168.1.10 -> 208.67.222.222
    const char* msg = "hello udp!";
    int mlen = (int)strlen(msg);

    int n = udp_pack(buf, 12345, 53, (const uint8_t*)msg, mlen, src, dst);
    expect("udp len", n == UDP_HDR_LEN + mlen);
    expect("udp sport", be16(buf + 0) == 12345);
    expect("udp dport", be16(buf + 2) == 53);
    expect("udp length", be16(buf + 4) == n);
    expect("udp csum good", udp_verify_checksum(buf, n, src, dst));
    buf[8] ^= 0xFF;
    expect("udp csum bad", !udp_verify_checksum(buf, n, src, dst));
    buf[8] ^= 0xFF;
    expect("udp payload", memcmp(buf + UDP_HDR_LEN, msg, mlen) == 0);

    // 校验和字段非零
    expect("udp csum nonzero", be16(buf + 6) != 0);

    // 解析回读
    UdpPacket p;
    int r = udp_parse(buf, n, src, dst, p, true);
    expect("udp parse", r == 0);
    expect("udp parse ports", p.hdr.src_port == 12345 && p.hdr.dst_port == 53);
    expect("udp parse data", p.data_len == mlen && memcmp(p.data, msg, mlen) == 0);

    // 破坏一字节应被校验和抓住
    uint8_t bad[256]; np_copy(bad, buf, n);
    bad[10] ^= 0x01;
    expect("udp csum detect", udp_parse(bad, n, src, dst, p, true) < 0);

    // 零长度 payload（仅头部）
    n = udp_pack(buf, 4000, 80, 0, 0, src, dst);
    expect("udp empty", n == UDP_HDR_LEN);
    r = udp_parse(buf, n, src, dst, p, true);
    expect("udp empty parse", r == 0 && p.data_len == 0);

    // 端口表
    UdpTable tbl;
    expect("udp bind ok", tbl.bind(53));
    expect("udp bind dup", !tbl.bind(53));
    expect("udp is_bound", tbl.is_bound(53) && !tbl.is_bound(54));
    tbl.close(53);
    expect("udp closed", !tbl.is_bound(53));
    expect("udp rebind", tbl.bind(53));

    return g_fails;
}

} // namespace netproto
} // namespace nefu
