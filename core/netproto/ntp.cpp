// ntp.cpp - NTP 客户端实现
#include "ntp.h"
#include "netproto_common.h"
#include <string.h>

namespace nefu {
namespace netproto {

int ntp_build_request(uint8_t* out, int out_len, uint32_t tx_sec) {
    if (!out || out_len < 48) return -1;
    np_zero(out, 48);
    // LI=0 (无警告), VN=4, Mode=3 (client)
    out[0] = (0 << 6) | (4 << 3) | NTP_MODE_CLIENT;
    out[1] = 0;     // stratum (client 不设)
    out[2] = 6;     // poll = 64 秒
    out[3] = -6;    // precision = 2^-6 s
    // 发送时间戳 (最后 8 字节)
    put_be32(out + 40, tx_sec);
    put_be32(out + 44, 0);   // frac = 0
    return 48;
}

int ntp_parse_response(const uint8_t* pkt, int len,
                       uint8_t& out_stratum,
                       uint32_t& out_server_sec,
                       int& out_roundtrip_ms) {
    if (!pkt || len < 48) return -1;
    const NtpPacket* p = (const NtpPacket*)pkt;
    uint8_t mode = p->li_vn_mode & 0x7;
    if (mode != NTP_MODE_SERVER) return -2;

    out_stratum = p->stratum;
    out_server_sec = be32(pkt + 40);

    // 往返时延 = (T4 - T1) - (T3 - T2)
    // T1 = orig (client 发送), T2 = rx (server 接收),
    // T3 = tx (server 发送), T4 = now (client 接收)
    // 简化: 用 server tx - client tx 近似
    // 这里返回固定的往返估计 (实际由调用方填入 T4)
    out_roundtrip_ms = 0;   // 调用方计算

    return 0;
}

// 自检
static int g_fail = 0;
static void expect(const char* name, bool ok) {
    if (!ok) g_fail++;
}

int ntp_self_test() {
    g_fail = 0;
    uint8_t buf[64];

    // 1) 构造请求
    uint32_t unix_now = 1700000000u;
    uint32_t ntp_now = unix_to_ntp(unix_now);
    int n = ntp_build_request(buf, sizeof(buf), ntp_now);
    expect("ntp req len", n == 48);
    expect("ntp req mode", (buf[0] & 0x7) == NTP_MODE_CLIENT);
    expect("ntp req vn", ((buf[0] >> 3) & 0x7) == 4);

    // 2) 纪元转换
    expect("ntp to unix", ntp_to_unix(ntp_now) == unix_now);
    expect("ntp unix off", NTP_UNIX_OFFSET == 2208988800u);

    // 3) 解析响应
    {
        uint8_t resp[48];
        np_zero(resp, sizeof(resp));
        resp[0] = (0 << 6) | (4 << 3) | NTP_MODE_SERVER;
        resp[1] = 2;    // stratum 2
        put_be32(resp + 40, ntp_now + 1);   // server tx
        put_be32(resp + 44, 0);

        uint8_t stratum;
        uint32_t srv_sec;
        int rtt;
        int r = ntp_parse_response(resp, 48, stratum, srv_sec, rtt);
        expect("ntp parse", r == 0);
        expect("ntp stratum", stratum == 2);
        expect("ntp srv sec", srv_sec == ntp_now + 1);
    }

    // 4) 错误情况
    {
        uint8_t bad[48];
        np_zero(bad, sizeof(bad));
        bad[0] = 3;   // 不是 server
        uint8_t s; uint32_t ts; int rtt;
        expect("ntp bad mode", ntp_parse_response(bad, 48, s, ts, rtt) < 0);
    }

    return g_fail;
}

} // namespace netproto
} // namespace nefu
