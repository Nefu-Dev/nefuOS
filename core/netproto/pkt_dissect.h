// nefuOS 网络协议栈 —— 报文解剖器
// 给一块原始以太网帧，逐层解析（以太网 -> IPv4 -> TCP/UDP/ICMP），
// 输出一行人类可读描述，供 netlab 展示。纯只读，不修改、不分配。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace netproto {

// 解剖一个以太网帧，写入 out（建议 >=256 字节）。返回写入长度。
int pkt_dissect_ethernet(const uint8_t* frame, int len, char* out, int outsz);
// 解剖一个裸 IP 包（无以太网头）。
int pkt_dissect_ip(const uint8_t* ip_pkt, int len, char* out, int outsz);

int pkt_dissect_self_test();

} // namespace netproto
} // namespace nefu
