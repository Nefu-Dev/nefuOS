// nefuOS 网络协议栈 —— 聚合头
// 包含本库全部模块，并提供统一的汇总自测入口 netproto_self_test()。
//
// 用法：
//   #include "netproto/netproto_all.h"
//   int fails = nefu::netproto::netproto_self_test();   // 0 = 全部通过
#pragma once

#include "netproto_common.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "dns.h"
#include "http.h"
#include "websocket.h"
#include "mqtt.h"
#include "dhcp.h"
#include "socket.h"
#include "firewall.h"
#include "ntp.h"
#include "netif.h"
#include "pktlog.h"
#include "netstack.h"
#include "pkt_dissect.h"

namespace nefu {
namespace netproto {

// 汇总：依次运行各模块自测，累加失败数并打印一行总结。
// 返回总失败数（0 = 全部通过）。
int netproto_self_test();

} // namespace netproto
} // namespace nefu
