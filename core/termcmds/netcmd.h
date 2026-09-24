// =============================================================================
//  netcmd.h —— 网络命令: ping/ifconfig/route/arp/netstat/nslookup/curl
// -----------------------------------------------------------------------------
//  物理发包在本库内不可用, 但以下逻辑是真实可测的:
//    * ipv4_parse / ipv4_format  —— 点分十进制与 uint32 互转
//    * icmp_checksum             —— 16 位反码求和校验和
//    * route_lookup              —— 路由表最长前缀匹配
//    * http_parse_response       —— 解析 "HTTP/1.1 200 OK\r\n..\r\n\r\nbody"
// =============================================================================
#pragma once

#include "termcmds_all.h"

namespace nefu {
namespace termcmds {

// ---- IPv4 工具 ----
// 解析 "a.b.c.d" 为网络字节序? 这里用主机序 uint32。失败返回 false。
bool ipv4_parse(const char* s, uint32_t* out);
void ipv4_format(uint32_t ip, char* out, int outsz);

// ICMP/IP 通用 16 位反码求和校验和
uint16_t internet_checksum(const uint8_t* data, int len);

// ---- 路由表(最长前缀匹配) ----
struct RouteEntry {
    uint32_t dest;    // 主机序
    uint32_t mask;
    uint32_t gw;
    char     iface[16];
};
// 查表: 命中返回下一跳 gw 并填 iface; 默认路由兜底。
uint32_t route_lookup(uint32_t ip, char* iface_out, int iface_cap);
// 增删路由(self_test 用)
void route_table_reset();
void route_table_add(uint32_t dest, uint32_t mask, uint32_t gw, const char* iface);

// ---- ARP 表 ----
struct ArpEntry { uint32_t ip; uint8_t mac[6]; };
void arp_table_reset();
bool arp_lookup(uint32_t ip, uint8_t mac_out[6]);
void arp_add(uint32_t ip, const uint8_t mac[6]);

// ---- DNS 表 ----
bool dns_lookup(const char* name, uint32_t* out_ip);

// ---- HTTP 响应解析 ----
struct HttpResp {
    int      status;       // 200/404/...
    nefu::String status_line;
    nefu::String headers;  // 原始头(每行)
    nefu::String body;
};
// 把一段收到的 HTTP 报文解析成结构。返回是否是合法 HTTP 响应。
bool http_parse_response(const char* raw, int len, HttpResp* out);

int netcmd_self_test();

} // namespace termcmds
} // namespace nefu
