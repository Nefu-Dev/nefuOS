// nefuOS 网络协议栈 —— IPv4（RFC 791）
//
// 头部结构（最小 20 字节）：
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |Version|  IHL  |    TOS      |        Total Length           |
//   +---------------+---------------+---------------+---------------+
//   |      Identification           |Flags|    Fragment Offset     |
//   +---------------+---------------+---------------+---------------+
//   |   TTL   | Protocol |       Header Checksum             |
//   +---------------+---------------+---------------+---------------+
//   |                        Source IP Address                    |
//   +-------------------------------------------------------------+
//   |                     Destination IP Address                  |
//   +-------------------------------------------------------------+
//
// 本模块：IP 地址工具、头部封装/解析/校验和、按 MTU 分片、分片重组。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

const int IP_HDR_LEN = 20;

// 协议号
const uint8_t IPPROTO_ICMP = 1;
const uint8_t IPPROTO_TCP  = 6;
const uint8_t IPPROTO_UDP  = 17;

// 标志位（写在 16 位 flags+offset 字的高 3 位）
const uint16_t IP_FLAG_DF = 0x4000;   // 禁止分片
const uint16_t IP_FLAG_MF = 0x2000;   // 还有更多分片

// ===================== IP 地址工具 =====================
// 解析 "a.b.c.d" 为主机序 32 位。失败返回 false。
bool ip_parse(const char* s, uint32_t& out);
// 主机序 IP -> "a.b.c.d"，buf 至少 16 字节，返回 buf。
char* ip_format(uint32_t ip, char* buf, int bufsz);

bool ip_is_multicast(uint32_t ip);   // 224.0.0.0/4
bool ip_is_broadcast(uint32_t ip);   // 255.255.255.255
bool ip_is_private(uint32_t ip);     // 10/8, 172.16/12, 192.168/16
bool ip_is_loopback(uint32_t ip);    // 127/8
// 判断 ip 是否在 (net, mask) 子网内
bool ip_in_subnet(uint32_t ip, uint32_t net, uint32_t mask);

// 解析 CIDR "a.b.c.d/n" -> net 与 mask。成功返回 0。
int ip_parse_cidr(const char* s, uint32_t& net, uint32_t& mask);
// 前缀长度（如 /24）转掩码：24 -> 0xFFFFFF00
uint32_t ip_prefix_to_mask(int prefix);
// 掩码转前缀长度：0xFFFFFF00 -> 24
int ip_mask_to_prefix(uint32_t mask);

// ===================== IP 头部 =====================
struct IpHeader {
    uint8_t  version;      // 恒为 4
    uint8_t  ihl;          // 头部长度（字节），通常 20
    uint8_t  tos;
    uint16_t total_len;    // 整包长度（头部+数据）
    uint16_t ident;
    uint16_t flags;        // 低 13 位是分片偏移；高 3 位是 DF/MF
    uint8_t  ttl;
    uint8_t  protocol;
    uint32_t src;          // 主机序
    uint32_t dst;          // 主机序
};

// 封装 20 字节 IP 头部（自动计算并写入校验和）。返回头部长度。
int ip_pack_header(uint8_t* out, uint16_t ident, uint8_t ttl, uint8_t protocol,
                   uint32_t src, uint32_t dst, uint16_t total_len,
                   uint16_t flags_fragoff);

// 解析 IP 头部。verify_csum=true 时校验头部校验和。
// 成功返回头部长度（>=20），失败返回 -1。
int ip_parse_header(const uint8_t* pkt, int len, IpHeader& out, bool verify_csum = true);

// 单独计算/校验头部校验和（pkt 指向 IP 头，hdr_len 字节）。
uint16_t ip_header_checksum(const uint8_t* pkt, int hdr_len);

// 协议号转名字
const char* ip_proto_name(uint8_t p);

// 转发一个 IP 包：TTL--，重算头部校验和。返回剩余 TTL；TTL<=0 返回 -1。
// pkt 指向整包（>=20 字节），原地修改。
int ip_forward_dec_ttl(uint8_t* pkt, int len);

// 从 flags+offset 字提取分片标志
inline bool ip_df_set(uint16_t fo) { return (fo & 0x4000u) != 0; }
inline bool ip_mf_set(uint16_t fo) { return (fo & 0x2000u) != 0; }
inline uint16_t ip_frag_offset(uint16_t fo) { return fo & 0x1FFFu; }

// ===================== 分片 =====================
// 一个分片（输出）
struct IpFrag {
    uint8_t* data;     // 整个分片 IP 包（含 IP 头），长度 len
    int      len;
};

// 把一个完整 IP 数据报按 mtu 切片。frags 输出每个分片（data 用 new[] 分配，
// 调用方负责 delete[]）。返回分片个数；失败返回 -1。
// 注意：会在需要时把最后一片之前的 payload 长度对齐到 8 字节边界。
int ip_fragment(const uint8_t* full, int full_len, int mtu, List<IpFrag>& frags);

// 释放 ip_fragment 产生的分片列表
void ip_frags_free(List<IpFrag>& frags);

// ===================== 分片重组 =====================
struct IpReassembler {
    IpReassembler();
    ~IpReassembler();

    // 喂入一个分片（指向完整 IP 分片包）。
    // 组装成功时：返回组装后的整包长度（>0），*out 指向 new[] 出的完整数据报。
    // 尚未集齐返回 0；输入非法返回 -1。
    int add(const uint8_t* pkt, int len, uint8_t** out);

    void clear();
    int  group_count() const;

private:
    struct Block { int off; int len; uint8_t* data; };
    struct Group {
        uint16_t ident; uint32_t src, dst; uint8_t proto;
        int total;            // -1 表示还没收到尾片
        IpHeader base;
        List<Block> blocks;
    };
    List<Group> groups_;
    int  find_group(uint16_t ident, uint32_t src, uint32_t dst, uint8_t proto);
    void free_group(Group& g);
};

// ===================== 路由表 =====================
struct RouteEntry {
    uint32_t net;
    uint32_t mask;
    uint32_t gateway;
    int      prefix;
    int      metric;
    char     iface[8];
};

class RouteTable {
public:
    RouteTable() : routes_() {}
    int  add(uint32_t net, uint32_t mask, uint32_t gateway, int metric, const char* iface);
    int  add_cidr(const char* cidr, uint32_t gateway, const char* iface);
    bool lookup(uint32_t dst, RouteEntry& out) const;
    int  size() const { return routes_.size(); }
    void clear() { routes_.clear(); }
private:
    List<RouteEntry> routes_;
};

int ip_self_test();

} // namespace netproto
} // namespace nefu
