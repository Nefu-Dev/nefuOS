// ntp.h - NTP 客户端 (RFC 5905, 简化版)
// 仅构造/解析 NTP 报文，用 Q16.16 定点计算偏移
#ifndef NEFU_NETPROTO_NTP_H
#define NEFU_NETPROTO_NTP_H

#include <stdint.h>
#include "netproto_common.h"

namespace nefu {
namespace netproto {

// NTP 报文 (48 字节)
struct NtpPacket {
    uint8_t  li_vn_mode;   // LI(2) | VN(3) | Mode(3)
    uint8_t  stratum;      // 层级
    uint8_t  poll;         // 轮询间隔 (2 的幂)
    int8_t   precision;    // 精度 (2 的幂, 秒)
    uint32_t root_delay;   // 根延迟 (定点)
    uint32_t root_disp;    // 根弥散 (定点)
    uint32_t ref_id;       // 参考标识
    uint32_t ref_ts_sec;   // 参考时间戳 (秒)
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;  // originate 时间戳
    uint32_t orig_ts_frac;
    uint32_t rx_ts_sec;    // 接收时间戳
    uint32_t rx_ts_frac;
    uint32_t tx_ts_sec;    // 发送时间戳
    uint32_t tx_ts_frac;
};

// 模式
enum NtpMode : uint8_t {
    NTP_MODE_CLIENT = 3,
    NTP_MODE_SERVER = 4,
};

// 构造 NTP client 请求
// out: 输出缓冲 (至少 48 字节)
// tx_sec: 客户端发送时间 (NTP 纪元秒, 1900 起)
// 返回写入字节数 (固定 48)
int ntp_build_request(uint8_t* out, int out_len, uint32_t tx_sec);

// 解析 NTP 响应
// out_stratum: 服务器层级
// out_server_sec: 服务器发送时间戳 (秒)
// out_roundtrip_ms: 往返时延 (毫秒, 整数)
// 返回 0 成功, <0 失败
int ntp_parse_response(const uint8_t* pkt, int len,
                       uint8_t& out_stratum,
                       uint32_t& out_server_sec,
                       int& out_roundtrip_ms);

// NTP 纪元 (1900) 到 Unix 纪元 (1970) 的偏移秒数
#define NTP_UNIX_OFFSET 2208988800u

// NTP 秒 -> Unix 秒
inline uint32_t ntp_to_unix(uint32_t ntp_sec) {
    return ntp_sec - NTP_UNIX_OFFSET;
}

// Unix 秒 -> NTP 秒
inline uint32_t unix_to_ntp(uint32_t unix_sec) {
    return unix_sec + NTP_UNIX_OFFSET;
}

// 自检
int ntp_self_test();

} // namespace netproto
} // namespace nefu

#endif // NEFU_NETPROTO_NTP_H
