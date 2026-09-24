// nefuOS 网络协议栈 —— ICMP 实现
#include "icmp.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

int icmp_build_echo_request(uint8_t* out, uint16_t id, uint16_t seq,
                            const uint8_t* payload, int payload_len) {
    if (!out) return -1;
    out[0] = ICMP_ECHO_REQUEST;
    out[1] = 0;                       // code
    put_be16(out + 2, 0);             // 校验和先置 0
    put_be16(out + 4, id);
    put_be16(out + 6, seq);
    if (payload && payload_len > 0) np_copy(out + 8, payload, payload_len);
    int total = 8 + payload_len;
    uint16_t c = csum_of(out, total);
    put_be16(out + 2, c);
    return total;
}

int icmp_build_echo_reply(uint8_t* out, const uint8_t* request, int request_len) {
    if (!out || !request || request_len < 8) return -1;
    np_copy(out, request, request_len);
    out[0] = ICMP_ECHO_REPLY;
    out[1] = 0;
    put_be16(out + 2, 0);
    put_be16(out + 2, csum_of(out, request_len));
    return request_len;
}

int icmp_parse(const uint8_t* pkt, int len, IcmpPacket& out) {
    if (!pkt || len < 8) return -1;
    out.type = pkt[0];
    out.code = pkt[1];
    out.checksum = be16(pkt + 2);
    out.echo.data = 0;
    out.echo.data_len = 0;
    // 校验和校验：对整段求和应得 0
    if (csum_of(pkt, len) != 0) return -1;

    if (out.type == ICMP_ECHO_REQUEST || out.type == ICMP_ECHO_REPLY) {
        out.echo.identifier = be16(pkt + 4);
        out.echo.sequence   = be16(pkt + 6);
        out.echo.data       = pkt + 8;
        out.echo.data_len  = len - 8;
    }
    return 0;
}

const char* icmp_type_name(uint8_t t) {
    switch (t) {
    case ICMP_ECHO_REPLY:   return "Echo Reply";
    case ICMP_DST_UNREACH:  return "Dest Unreachable";
    case ICMP_ECHO_REQUEST: return "Echo Request";
    case ICMP_TIME_EXCEEDED:return "Time Exceeded";
    case ICMP_TS_REQUEST:   return "Timestamp Request";
    case ICMP_TS_REPLY:     return "Timestamp Reply";
    default:                return "?";
    }
}

int icmp_build_timestamp_request(uint8_t* out, uint16_t id, uint16_t seq,
                                uint32_t originate, uint32_t rx, uint32_t tx) {
    if (!out) return -1;
    out[0] = ICMP_TS_REQUEST;
    out[1] = 0;
    put_be16(out + 2, 0);
    put_be16(out + 4, id);
    put_be16(out + 6, seq);
    put_be32(out + 8, originate);
    put_be32(out + 12, rx);
    put_be32(out + 16, tx);
    int total = 20;
    put_be16(out + 2, csum_of(out, total));
    return total;
}

int icmp_build_dest_unreachable(uint8_t* out, uint8_t code,
                               const uint8_t* original_ip_pkt, int original_len) {
    if (!out || !original_ip_pkt || original_len < 8) return -1;
    out[0] = ICMP_DST_UNREACH;
    out[1] = code;
    put_be16(out + 2, 0);
    put_be16(out + 4, 0);     // unused
    put_be16(out + 6, 0);     // unused
    // 携带原始 IP 头 + 前 8 字节（至少）
    int copy_len = original_len;
    if (copy_len > 56) copy_len = 56;   // 控制大小
    np_copy(out + 8, original_ip_pkt, copy_len);
    int total = 8 + copy_len;
    put_be16(out + 2, csum_of(out, total));
    return total;
}

int icmp_build_time_exceeded(uint8_t* out, const uint8_t* original_ip_pkt, int original_len) {
    if (!out || !original_ip_pkt || original_len < 8) return -1;
    out[0] = ICMP_TIME_EXCEEDED;
    out[1] = 0;
    put_be16(out + 2, 0);
    put_be16(out + 4, 0);
    put_be16(out + 6, 0);
    int copy_len = original_len;
    if (copy_len > 56) copy_len = 56;
    np_copy(out + 8, original_ip_pkt, copy_len);
    int total = 8 + copy_len;
    put_be16(out + 2, csum_of(out, total));
    return total;
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [icmp] FAIL: %s\n", what); }
}
} // namespace

int icmp_self_test() {
    g_fails = 0;
    uint8_t buf[128];

    // 1) 构造 echo request
    const char* pingdata = "nefuOS-ping-payload";
    int plen = (int)strlen(pingdata);
    int n = icmp_build_echo_request(buf, 0x1234, 5, (const uint8_t*)pingdata, plen);
    expect("icmp req len", n == 8 + plen);
    expect("icmp req type", buf[0] == ICMP_ECHO_REQUEST);
    expect("icmp req id", be16(buf + 4) == 0x1234);
    expect("icmp req seq", be16(buf + 6) == 5);
    expect("icmp req payload", memcmp(buf + 8, pingdata, plen) == 0);

    // 2) 校验和正确
    expect("icmp csum", csum_of(buf, n) == 0);

    // 3) 解析回读
    IcmpPacket p;
    int r = icmp_parse(buf, n, p);
    expect("icmp parse", r == 0);
    expect("icmp parse type", p.type == ICMP_ECHO_REQUEST);
    expect("icmp parse id", p.echo.identifier == 0x1234 && p.echo.sequence == 5);
    expect("icmp parse data", p.echo.data_len == plen &&
           memcmp(p.echo.data, pingdata, plen) == 0);

    // 4) 故意破坏一字节，校验和应失败
    uint8_t bad[128]; np_copy(bad, buf, n);
    bad[10] ^= 0x55;
    expect("icmp csum detect", icmp_parse(bad, n, p) == -1);

    // 5) echo reply：把 request 原样回包
    uint8_t reply[128];
    int rl = icmp_build_echo_reply(reply, buf, n);
    expect("icmp reply len", rl == n);
    expect("icmp reply type", reply[0] == ICMP_ECHO_REPLY);
    expect("icmp reply csum", csum_of(reply, rl) == 0);
    r = icmp_parse(reply, rl, p);
    expect("icmp reply parse", r == 0 && p.type == ICMP_ECHO_REPLY);
    expect("icmp reply echo", p.echo.identifier == 0x1234 && p.echo.sequence == 5);
    expect("icmp reply data", p.echo.data_len == plen &&
           memcmp(p.echo.data, pingdata, plen) == 0);

    // 6) 太短
    expect("icmp too short", icmp_parse(buf, 4, p) == -1);

    // 7) 名字
    expect("type name", strcmp(icmp_type_name(ICMP_ECHO_REQUEST), "Echo Request") == 0);

    // 8) 时间戳请求
    n = icmp_build_timestamp_request(buf, 0x1111, 3, 1000, 2000, 3000);
    expect("icmp ts len", n == 20);
    expect("icmp ts type", buf[0] == ICMP_TS_REQUEST);
    expect("icmp ts orig", be32(buf + 8) == 1000);
    expect("icmp ts csum", csum_of(buf, n) == 0);

    // 9) 目的不可达（带原始 IP 头）
    uint8_t origip[40];
    for (int i = 0; i < 40; i++) origip[i] = (uint8_t)(i * 7);
    n = icmp_build_dest_unreachable(buf, ICMP_PORT_UNREACH, origip, 40);
    expect("icmp unreach type", buf[0] == ICMP_DST_UNREACH);
    expect("icmp unreach code", buf[1] == ICMP_PORT_UNREACH);
    expect("icmp unreach carry", memcmp(buf + 8, origip, 40) == 0);
    expect("icmp unreach csum", csum_of(buf, n) == 0);
    uint8_t buf2[80];
    n = icmp_build_time_exceeded(buf2, origip, 40);
    expect("icmp timex type", buf2[0] == ICMP_TIME_EXCEEDED && csum_of(buf2, n) == 0);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
