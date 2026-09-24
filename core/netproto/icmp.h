// nefuOS 网络协议栈 —— ICMP（RFC 792）
//
// ICMP 报文结构（IPv4 之上，紧随 IP 头）：
//   类型(1) 代码(1) 校验和(2)  其余(4，语义随类型)
//
// 本模块重点实现 echo request / echo reply（ping），
// 以及目的不可达等常见类型的封装/解析。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace netproto {

// ICMP 类型
const uint8_t ICMP_ECHO_REPLY    = 0;
const uint8_t ICMP_DST_UNREACH    = 3;
const uint8_t ICMP_ECHO_REQUEST   = 8;
const uint8_t ICMP_TIME_EXCEEDED  = 11;

// 目的不可达代码
const uint8_t ICMP_NET_UNREACH    = 0;
const uint8_t ICMP_HOST_UNREACH   = 1;
const uint8_t ICMP_PROTO_UNREACH  = 2;
const uint8_t ICMP_PORT_UNREACH   = 3;

struct IcmpEcho {
    uint16_t identifier;
    uint16_t sequence;
    const uint8_t* data;   // 指向包内数据（不持有）
    int      data_len;
};

// 封装一个 echo request。out 至少 8 + payload_len 字节。
// 返回总长度（8 + payload_len）。
int icmp_build_echo_request(uint8_t* out, uint16_t id, uint16_t seq,
                            const uint8_t* payload, int payload_len);

// 封装一个 echo reply（把 request 原样回显）。
int icmp_build_echo_reply(uint8_t* out, const uint8_t* request, int request_len);

// 解析一个 ICMP 报文。成功返回 0；校验和错/长度不足返回 -1。
// type/code 始终被填充。若为 echo（请求或应答），填充 echo。
struct IcmpPacket {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    IcmpEcho echo;
};
int icmp_parse(const uint8_t* pkt, int len, IcmpPacket& out);

// 类型 -> 名字
const char* icmp_type_name(uint8_t t);

// 时间戳请求/应答（type 13/14）。其余 8 字节 = id+seq，后接 3 个 32 位时间戳。
const uint8_t ICMP_TS_REQUEST  = 13;
const uint8_t ICMP_TS_REPLY    = 14;
int icmp_build_timestamp_request(uint8_t* out, uint16_t id, uint16_t seq,
                                uint32_t originate, uint32_t rx, uint32_t tx);

// 目的不可达：带上触发它的原始 IP 头 + 前 8 字节（RFC 1122 要求）。
int icmp_build_dest_unreachable(uint8_t* out, uint8_t code,
                               const uint8_t* original_ip_pkt, int original_len);
// 时间超时（TTL=0 转发时回送）。
int icmp_build_time_exceeded(uint8_t* out, const uint8_t* original_ip_pkt, int original_len);

int icmp_self_test();

} // namespace netproto
} // namespace nefu
