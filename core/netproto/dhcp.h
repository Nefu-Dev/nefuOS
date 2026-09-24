// dhcp.h - DHCP/BOOTP 客户端协议 (RFC 2131)
// 仅构造/解析 DHCP 报文，不做实际收发
#ifndef NEFU_NETPROTO_DHCP_H
#define NEFU_NETPROTO_DHCP_H

#include <stdint.h>
#include "netproto_common.h"

namespace nefu {
namespace netproto {

// DHCP 操作码
enum DhcpOp : uint8_t {
    DHCP_OP_BOOTREQUEST = 1,
    DHCP_OP_BOOTREPLY   = 2,
};

// DHCP 消息类型 (option 53)
enum DhcpMsgType : uint8_t {
    DHCP_DISCOVER = 1,
    DHCP_OFFER    = 2,
    DHCP_REQUEST  = 3,
    DHCP_DECLINE  = 4,
    DHCP_ACK      = 5,
    DHCP_NAK      = 6,
    DHCP_RELEASE  = 7,
    DHCP_INFORM   = 8,
};

// DHCP 选项号
enum DhcpOpt : uint8_t {
    DHCP_OPT_SUBNET_MASK = 1,
    DHCP_OPT_ROUTER      = 3,
    DHCP_OPT_DNS         = 6,
    DHCP_OPT_HOSTNAME    = 12,
    DHCP_OPT_DOMAIN_NAME = 15,
    DHCP_OPT_REQUEST_IP  = 50,
    DHCP_OPT_LEASE_TIME  = 51,
    DHCP_OPT_MSG_TYPE    = 53,
    DHCP_OPT_SERVER_ID   = 54,
    DHCP_OPT_PARAM_LIST  = 55,
    DHCP_OPT_END         = 255,
};

// DHCP 报文头部 (236 字节固定部分 + 选项)
struct DhcpPacket {
    uint8_t  op;          // 1=request, 2=reply
    uint8_t  htype;       // 硬件类型 (1=以太网)
    uint8_t  hlen;        // 硬件地址长度 (6)
    uint8_t  hops;        // 跳数
    uint32_t xid;         // 事务 ID
    uint16_t secs;        // 已消耗秒数
    uint16_t flags;       // 标志位
    uint32_t ciaddr;      // 客户端 IP
    uint32_t yiaddr;      // 你的 IP (分配给客户端)
    uint32_t siaddr;      // 服务器 IP
    uint32_t giaddr;      // 中继代理 IP
    uint8_t  chaddr[16];  // 客户端硬件地址
    uint8_t  sname[64];   // 服务器名
    uint8_t  file[128];   // 启动文件名
    uint32_t magic;       // 魔数 0x63825363
};

// 构造 DHCP DISCOVER 报文
// client_mac: 客户端 MAC (6 字节)
// xid: 事务 ID
// 返回写入字节数，<0 表示错误
int dhcp_build_discover(uint8_t* out, int out_len,
                        const uint8_t* client_mac, uint32_t xid);

// 构造 DHCP REQUEST 报文 (选择 offer 后)
int dhcp_build_request(uint8_t* out, int out_len,
                       const uint8_t* client_mac, uint32_t xid,
                       uint32_t requested_ip, uint32_t server_id);

// 构造 DHCP RELEASE 报文
int dhcp_build_release(uint8_t* out, int out_len,
                       const uint8_t* client_mac, uint32_t xid,
                       uint32_t assigned_ip, uint32_t server_id);

// 解析 DHCP 响应 (OFFER/ACK)
// out_ip: 分配到的 IP (yiaddr)
// out_server: 服务器 IP (option 54)
// out_lease: 租约秒数 (option 51)
// out_mask: 子网掩码 (option 1)
// out_gw: 网关 (option 3)
// 返回消息类型 (DhcpMsgType)，<0 表示错误
int dhcp_parse_reply(const uint8_t* pkt, int len,
                     uint32_t& out_ip, uint32_t& out_server,
                     uint32_t& out_lease, uint32_t& out_mask,
                     uint32_t& out_gw);

// 消息类型名
const char* dhcp_msg_type_name(uint8_t t);

// 自检，返回失败数
int dhcp_self_test();

} // namespace netproto
} // namespace nefu

#endif // NEFU_NETPROTO_DHCP_H
