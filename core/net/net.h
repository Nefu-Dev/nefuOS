/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS network stack: e1000 NIC + ARP + ICMP + minimal TCP (bare x86_64)
#pragma once
#include <stdint.h>

namespace nefu {

struct NetIf {
    bool up;
    uint8_t mac[6];
    char name[48];      // NIC model name ("e1000"/"rtl8139"/"virtio-net")
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

// Minimal UDP/DNS support (no retransmit/reorder). Used by the HTTP client's
// DNS fallback so arbitrary hosts resolve on a real network.
// Resolve <name> by querying each server in <servers> (UDP/53); returns the
// first A record as a host-order IPv4, or 0 on failure.
uint32_t dns_resolve_multi(const char* name, const uint32_t* servers, int ns);

// Resolve <name>: static table first, then a real DNS query. Returns a
// host-order IPv4, or 0 on failure. When the name resolves to several
// addresses (CDNs do), successive calls hand out a different one so that a
// caller which retries reaches another edge node.
uint32_t dns_lookup(const char* name);

// Resolve <name> and return every address the resolver saw (up to max).
// Returns the number of addresses written to out_ips.
int dns_lookup_multi(const char* name, uint32_t* out_ips, int max);

} // namespace nefu

