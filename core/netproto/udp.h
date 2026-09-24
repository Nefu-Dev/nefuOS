// nefuOS 网络协议栈 —— UDP（RFC 768）
//
// 头部（8 字节）：
//   源端口(2) 目的端口(2) 长度(2) 校验和(2)
//
// UDP 校验和需要叠加 IPv4 伪首部（见 common.h 的 transport_csum）。
// IPv4 下校验和可选——若为 0 表示未计算；本模块默认总是计算并写入。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

const int UDP_HDR_LEN = 8;

struct UdpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    int      length;       // UDP 头+数据总长度
};

// 封装 UDP 数据报。out 至少 8 + payload_len 字节。
// src_ip/dst_ip 为主机序，用于伪首部校验和。
// 返回写入总长度（8 + payload_len）。
int udp_pack(uint8_t* out, uint16_t src_port, uint16_t dst_port,
             const uint8_t* payload, int payload_len,
             uint32_t src_ip, uint32_t dst_ip);

// 解析 UDP 报文。成功返回 0，失败 -1。
// out 中 data 指向包内 payload（不持有）。
struct UdpPacket {
    UdpHeader hdr;
    const uint8_t* data;
    int      data_len;
};
int udp_parse(const uint8_t* seg, int len, uint32_t src_ip, uint32_t dst_ip,
              UdpPacket& out, bool verify_csum = true);

// 校验入站 UDP 校验和。注意 IPv4 下校验和可为 0（表示未计算）。
bool udp_verify_checksum(const uint8_t* seg, int seg_len,
                         uint32_t src_ip, uint32_t dst_ip);

int udp_self_test();

// ===================== UDP 端口表 =====================
// 极简 UDP 端口占用/解复用表（bind/lookup/close）。
struct UdpEndpoint {
    uint16_t port;
    bool     bound;
};

class UdpTable {
public:
    UdpTable() : eps_() {}
    // 绑定端口。已占用返回 false。
    bool bind(uint16_t port);
    bool is_bound(uint16_t port) const;
    void close(uint16_t port);
    int  size() const { return eps_.size(); }
    void clear() { eps_.clear(); }
private:
    List<UdpEndpoint> eps_;
};

} // namespace netproto
} // namespace nefu
