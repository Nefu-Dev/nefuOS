// nefuOS network stack: e1000 NIC + ARP + ICMP + minimal TCP (bare x86_64)
#pragma once
#include <stdint.h>

namespace nefu {

struct NetIf {
    bool up;
    uint8_t mac[6];
    uint32_t ip;        // host order
    uint32_t gw;        // host order
    uint32_t netmask;   // host order
    uint8_t gw_mac[6];
    uint32_t rx_count, tx_count;
    uint32_t arp_reqs, icmp_reqs, tcp_conns;
};

extern NetIf g_net;

bool net_init();          // probe PCI e1000, bring link up, start rings
void net_poll();          // drain RX, dispatch ARP/ICMP/TCP
bool net_ping(uint32_t ip, int timeout_ms);  // ICMP echo, true on reply

// minimal TCP (no retransmit/reorder; fine on local slirp)
int  tcp_connect(uint32_t ip, uint16_t port, int timeout_ms); // -> fd or -1
int  tcp_send(int fd, const void* data, int len);
int  tcp_recv(int fd, void* buf, int maxlen, int timeout_ms);
void tcp_close(int fd);

uint16_t net_checksum(const void* data, int len);   // one's complement
uint16_t net_ip_checksum(const uint8_t* ip_hdr);    // 20B IP header

} // namespace nefu
