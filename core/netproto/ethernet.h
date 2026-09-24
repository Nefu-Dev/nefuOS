// nefuOS 网络协议栈 —— 以太网（Ethernet II，RFC 894 / IEEE 802.3）
//
// 帧结构（不含 NIC 剥离的 4 字节 FCS）：
//   +-----------------+-----------------+-----------+-----------------+
//   | 目的 MAC (6B)   | 源 MAC (6B)     | 类型(2B)  |  payload (46~1500B) |
//   +-----------------+-----------------+-----------+-----------------+
//   头部固定 14 字节。payload 最少 46 字节（不足补零填充），最大 1500（MTU）。
//
// 本模块负责：MAC 地址的解析/格式化/比较/分类，以太网头部的封装与解析，
// 以及以太类型常量。不涉及驱动发送——只做"把字节摆对位置"。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace netproto {

// ---- 以太类型（EtherType）----
const uint16_t ET_IPV4 = 0x0800;
const uint16_t ET_ARP  = 0x0806;
const uint16_t ET_VLAN = 0x8100;   // 802.1Q 标签
const uint16_t ET_IPV6 = 0x86DD;

const int ETH_HDR_LEN   = 14;   // 以太网头部长度
const int ETH_MTU       = 1500; // payload 最大长度
const int ETH_MIN_PAYLOAD = 46; // payload 最小长度（不足填充）
const int ETH_MIN_FRAME = 60;   // 头部+payload 的最小帧长（不含 FCS）

// ---- MAC 地址 ----
struct MacAddr {
    uint8_t oct[6];

    bool operator==(const MacAddr& o) const;
    bool operator!=(const MacAddr& o) const { return !(*this == o); }

    bool is_zero() const;
    bool is_broadcast() const;              // FF:FF:FF:FF:FF:FF
    bool is_multicast() const;              // 首字节最低位 = 1
    bool is_unicast() const { return !is_multicast(); }
    bool is_local_admin() const;            // 首字节次低位 = 1（本地管理地址）
};

// 解析 "AA:BB:CC:DD:EE:FF"（也接受 - 分隔）。成功返回 true 并写入 out。
bool mac_parse(const char* s, MacAddr& out);
// 格式化为 "AA:BB:CC:DD:EE:FF"，buf 至少 18 字节，返回 buf。
char* mac_format(const MacAddr& m, char* buf, int bufsz);
// 全零 / 广播 常量构造
MacAddr mac_zero();
MacAddr mac_broadcast();

// ---- 以太网帧 ----
struct EthHeader {
    MacAddr dst;
    MacAddr src;
    uint16_t ethertype;
};

// 把头部封装到 out（至少 14 字节），返回写入字节数（恒为 ETH_HDR_LEN）。
int eth_pack_header(uint8_t* out, const MacAddr& dst, const MacAddr& src, uint16_t ethertype);

// 从 pkt 解析头部。len 为整帧长度。成功返回 14（头部长度）并填充 h；
// 长度不足返回 -1。
int eth_parse_header(const uint8_t* pkt, int len, EthHeader& h);

// 按 MTU 把 payload 长度补齐到最小值所需的填充字节数（>=0）。
int eth_pad_needed(int payload_len);

// 以太类型转可读名字（调试用）。
const char* ethertype_name(uint16_t et);

// 封装完整帧：头部 + payload + 自动零填充到最小 60 字节。
// out 至少 60 字节。返回整帧长度。
int eth_pack_frame(uint8_t* out, const MacAddr& dst, const MacAddr& src,
                  uint16_t ethertype, const uint8_t* payload, int payload_len);

// ===================== 802.1Q VLAN 标签 =====================
int eth_pack_vlan_tag(uint8_t* out_after_src, uint16_t tci, uint16_t inner_ethertype);
bool eth_is_vlan(const EthHeader& h);

// 网卡统计计数器
struct EthStats {
    uint32_t tx_frames, rx_frames;
    uint32_t tx_bytes, rx_bytes;
    uint32_t rx_errors;
    uint32_t rx_unknown_type;
    void reset() { tx_frames=rx_frames=tx_bytes=rx_bytes=rx_errors=rx_unknown_type=0; }
};

// 模块自测，返回失败数（0 = 全部通过）。
int ethernet_self_test();

} // namespace netproto
} // namespace nefu
