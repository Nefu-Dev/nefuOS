// nefuOS network stack: e1000 (QEMU 82540EM) + ARP + ICMP + minimal TCP
// Pure integer, freestanding. Polled (no interrupts).
#include "net.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

NetIf g_net;

// ---------------- io ----------------
static inline uint8_t inb_p(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint32_t inl_p(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl_p(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline void io_wait_n() { outl_p(0x80, 0); }

// ---------------- PCI ----------------
static uint32_t pci_read32(int bus, int dev, int func, int reg) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    outl_p(0xCF8, addr);
    return inl_p(0xCFC);
}

static void pci_write32(int bus, int dev, int func, int reg, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    outl_p(0xCF8, addr);
    outl_p(0xCFC, val);
}

// ---------------- e1000 registers ----------------
#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_ICR    0x00C0
#define REG_IMS    0x00D0
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RA0    0x5400   // RAL
#define REG_RA1    0x5404   // RAH
#define REG_MTA    0x5200

#define CTRL_RST  0x04000000u
#define CTRL_SLU  0x00000040u
#define CTRL_ASDE 0x00000020u
#define RCTL_EN   0x00000002u
#define RCTL_SBP  0x00000004u
#define RCTL_UPE  0x00000008u
#define RCTL_MPE  0x00000010u
#define RCTL_BAM  0x00008000u
#define RCTL_SECRC 0x04000000u
#define TCTL_EN   0x00000002u
#define TCTL_PSP  0x00000008u
#define TCTL_CT   0x000000F0u
#define TCTL_COLD 0x003F0000u

#define RX_RING   32
#define TX_RING   16
#define RX_BUF    2048

struct RxDesc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct TxDesc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

static volatile uint32_t* s_mmio = 0;
static RxDesc* s_rx_desc = 0;
static TxDesc* s_tx_desc = 0;
static uint8_t* s_rx_buf[RX_RING];
static uint8_t* s_tx_buf[TX_RING];
static int s_tx_head = 0;   // next to use (TDT)

static inline uint32_t mmio32(uint32_t reg) { return s_mmio[reg / 4]; }
static inline void mmio_wr(uint32_t reg, uint32_t v) { s_mmio[reg / 4] = v; }

// ---------------- checksums ----------------
uint16_t net_checksum(const void* data, int len) {
    // standard big-endian (network-order) one's complement sum
    const uint8_t* b = (const uint8_t*)data;
    uint32_t sum = 0;
    int i = 0;
    for (; i + 1 < len; i += 2) sum += ((uint16_t)b[i] << 8) | b[i + 1];
    if (i < len) sum += (uint16_t)b[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    // return in network byte order so a plain uint16_t store emits the right bytes
    return __builtin_bswap16((uint16_t)~sum);
}
uint16_t net_ip_checksum(const uint8_t* ip_hdr) { return net_checksum(ip_hdr, 20); }

// ---------------- ethernet ----------------
struct EthHdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

static void eth_send(const uint8_t* dst, uint16_t type, const void* payload, int plen) {
    if (!s_mmio || !s_tx_desc) return;
    // wait for a free descriptor
    int slot = s_tx_head % TX_RING;
    uint32_t wait = 0;
    while (wait < 2000000u) {
        if (s_tx_desc[slot].status & 0x01) break;  // DD
        if (mmio32(REG_TDH) != (uint32_t)(mmio32(REG_TDT) % TX_RING)) break;
        wait++;
    }
    uint8_t* p = s_tx_buf[slot];
    EthHdr* eh = (EthHdr*)p;
    for (int i = 0; i < 6; i++) { eh->dst[i] = dst[i]; eh->src[i] = g_net.mac[i]; }
    eh->type = type;
    if (plen > 0 && payload) memcpy(p + 14, payload, (size_t)plen);
    int total = 14 + plen;
    if (total < 60) total = 60;  // pad to minimum
    s_tx_desc[slot].addr = (uint64_t)(uintptr_t)p;
    s_tx_desc[slot].length = (uint16_t)total;
    s_tx_desc[slot].cmd = 0x0B;   // EOP | IFCS | RS
    s_tx_desc[slot].status = 0;
    mmio_wr(REG_TDT, (uint32_t)((s_tx_head + 1) % TX_RING));
    s_tx_head = (s_tx_head + 1) % TX_RING;
    g_net.tx_count++;
}

// ---------------- ARP ----------------
#define ETH_ARP 0x0608
#define ETH_IP  0x0008

struct ArpPkt {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

static void arp_request(uint32_t ip) {
    if (!s_mmio) return;
    uint8_t pkt[42];
    memset(pkt, 0, sizeof(pkt));
    ArpPkt* a = (ArpPkt*)(pkt + 14);
    a->htype = 0x0100;
    a->ptype = ETH_IP;
    a->hlen = 6;
    a->plen = 4;
    a->oper = 0x0100;   // request
    for (int i = 0; i < 6; i++) { a->sha[i] = g_net.mac[i]; a->tha[i] = 0; }
    a->spa = __builtin_bswap32(g_net.ip);
    a->tpa = __builtin_bswap32(ip);
    uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    eth_send(bcast, ETH_ARP, pkt + 14, 28);
    g_net.arp_reqs++;
}

static void arp_reply(const uint8_t* dst_mac, uint32_t tpa_ip, const uint8_t* target_mac) {
    uint8_t pkt[42];
    memset(pkt, 0, sizeof(pkt));
    ArpPkt* a = (ArpPkt*)(pkt + 14);
    a->htype = 0x0100;
    a->ptype = ETH_IP;
    a->hlen = 6;
    a->plen = 4;
    a->oper = 0x0200;   // reply
    for (int i = 0; i < 6; i++) { a->sha[i] = g_net.mac[i]; a->tha[i] = target_mac ? target_mac[i] : dst_mac[i]; }
    a->spa = __builtin_bswap32(g_net.ip);
    a->tpa = __builtin_bswap32(tpa_ip);
    eth_send(dst_mac, ETH_ARP, pkt + 14, 28);
}

static void arp_handle(const uint8_t* frame, int len) {
    if (len < 42) return;
    const ArpPkt* a = (const ArpPkt*)(frame + 14);
    if (a->htype != 0x0100 || a->ptype != ETH_IP || a->hlen != 6 || a->plen != 4) return;
    uint32_t spa = a->spa;
    if (a->oper == 0x0100) {  // request: reply if it's for us
        if (spa == g_net.ip) return;
        if (a->tpa == g_net.ip) {
            arp_reply(frame, a->spa, a->sha);
        }
    } else if (a->oper == 0x0200) {  // reply: cache gateway MAC
        if (a->tpa == g_net.ip && spa == g_net.gw) {
            for (int i = 0; i < 6; i++) g_net.gw_mac[i] = a->sha[i];
        }
    }
}

// ---------------- IP / ICMP ----------------
struct IpPkt {
    uint8_t  vihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t hdr_csum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

static void ip_send(uint32_t dst, uint8_t proto, const void* payload, int plen, uint16_t id) {
    uint8_t pkt[1600];
    IpPkt* ip = (IpPkt*)pkt;
    memset(ip, 0, 20);
    ip->vihl = 0x45;
    ip->total_len = __builtin_bswap16((uint16_t)(20 + plen));
    ip->id = __builtin_bswap16(id);
    ip->ttl = 64;
    ip->proto = proto;
    ip->src = __builtin_bswap32(g_net.ip);
    ip->dst = __builtin_bswap32(dst);
    if (plen > 0) memcpy(pkt + 20, payload, (size_t)plen);
    ip->hdr_csum = 0;
    ip->hdr_csum = net_ip_checksum(pkt);
    // resolve gateway MAC
    bool have_gw = g_net.gw_mac[0] != 0 || g_net.gw_mac[1] != 0 || g_net.gw_mac[2] != 0;
    if (!have_gw) {
        arp_request(g_net.gw);
        // one quick poll to receive the reply
        for (int i = 0; i < 2000 && !(g_net.gw_mac[0] || g_net.gw_mac[1] || g_net.gw_mac[2]); i++)
            net_poll();
    }
    if (!(g_net.gw_mac[0] || g_net.gw_mac[1] || g_net.gw_mac[2])) {
        // no ARP reply: fall back to QEMU slirp's virtual gateway MAC
        static const uint8_t slirp_gw[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
        for (int i = 0; i < 6; i++) g_net.gw_mac[i] = slirp_gw[i];
        klogf("net: arp no reply, using slirp gw mac\n");
    }
    eth_send(g_net.gw_mac, ETH_IP, pkt, 20 + plen);
}

struct IcmpPkt {
    uint8_t type, code;
    uint16_t csum;
    uint16_t id, seq;
} __attribute__((packed));

static volatile bool s_icmp_done = false;
static uint16_t s_icmp_id = 0x1234;
static uint16_t s_icmp_seq = 0;

static void icmp_handle(const uint8_t* ip_pkt, int iplen) {
    const IpPkt* ip = (const IpPkt*)ip_pkt;
    int hl = (ip->vihl & 0x0F) * 4;
    int total = __builtin_bswap16(ip->total_len);
    if (total < hl + 8 || total > iplen) return;
    const IcmpPkt* ic = (const IcmpPkt*)(ip_pkt + hl);
    if (ic->type == 8) {  // echo request -> reply
        uint8_t buf[64];
        int plen = total - hl;
        if (plen > 64) plen = 64;
        memcpy(buf, ic, (size_t)plen);
        IcmpPkt* r = (IcmpPkt*)buf;
        r->type = 0;
        r->code = 0;
        r->csum = 0;
        r->csum = net_checksum(buf, plen);
        ip_send(__builtin_bswap32(ip->src), 1, buf, plen, (uint16_t)(__builtin_bswap16(ip->id) + 1));
        g_net.icmp_reqs++;
    } else if (ic->type == 0) {  // echo reply
        if (__builtin_bswap16(ic->id) == s_icmp_id && __builtin_bswap16(ic->seq) == s_icmp_seq) s_icmp_done = true;
    }
}

bool net_ping(uint32_t ip, int timeout_ms) {
    if (!g_net.up) return false;
    s_icmp_done = false;
    s_icmp_seq++;
    IcmpPkt ic;
    ic.type = 8; ic.code = 0; ic.csum = 0; ic.id = __builtin_bswap16(s_icmp_id); ic.seq = __builtin_bswap16(s_icmp_seq);
    ic.csum = net_checksum(&ic, 8);
    ip_send(ip, 1, &ic, 8, (uint16_t)(s_icmp_seq * 3));
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < (uint32_t)timeout_ms) {
        net_poll();
        if (s_icmp_done) return true;
    }
    return s_icmp_done;
}

// ---------------- TCP (minimal) ----------------
#define TCP_ESTABLISHED 1
#define TCP_SYN_SENT    2
#define TCP_FIN_WAIT    3
#define TCP_CLOSED      0

#define MAX_TCPCONN 4
#define TCP_RXBUFSZ 8192

struct TcpConn {
    int state;
    uint16_t local_port;
    uint32_t peer_ip;
    uint16_t peer_port;
    uint32_t snd_nxt;    // next seq to send
    uint32_t rcv_nxt;    // next seq expected
    uint8_t rxbuf[TCP_RXBUFSZ];
    int rx_len;
    bool rx_closed;
};

static TcpConn s_conns[MAX_TCPCONN];
static uint16_t s_next_port = 20000;

struct TcpHdr {
    uint16_t src, dst;
    uint32_t seq, ack;
    uint8_t  off_flags;
    uint8_t  flags;
    uint16_t win, csum, urp;
} __attribute__((packed));

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, const void* tcp, int len) {
    uint8_t pseudo[12];
    uint32_t s = __builtin_bswap32(src), d = __builtin_bswap32(dst);
    memcpy(pseudo, &s, 4);
    memcpy(pseudo + 4, &d, 4);
    pseudo[8] = 0; pseudo[9] = 6;
    pseudo[10] = (uint8_t)((uint16_t)len >> 8);
    pseudo[11] = (uint8_t)((uint16_t)len & 0xFF);
    uint32_t sum = 0;
    const uint8_t* b = pseudo;
    for (int i = 0; i + 1 < 12; i += 2) sum += ((uint16_t)b[i] << 8) | b[i + 1];
    b = (const uint8_t*)tcp;
    int i = 0;
    for (; i + 1 < len; i += 2) sum += ((uint16_t)b[i] << 8) | b[i + 1];
    if (i < len) sum += (uint16_t)b[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    // return in network byte order so a plain uint16_t store emits the right bytes
    return __builtin_bswap16((uint16_t)~sum);
}

static void tcp_send_seg(TcpConn* c, uint8_t flags, const void* data, int len, uint32_t seq) {
    uint8_t buf[1600];
    TcpHdr* t = (TcpHdr*)buf;
    memset(t, 0, 20);
    t->src = __builtin_bswap16(c->local_port);
    t->dst = __builtin_bswap16(c->peer_port);
    t->seq = __builtin_bswap32(seq);
    t->ack = __builtin_bswap32(c->rcv_nxt);
    t->off_flags = 0x50;
    t->flags = flags;
    t->win = __builtin_bswap16(0x4000);
    if (len > 0) memcpy(buf + 20, data, (size_t)len);
    t->csum = 0;
    t->csum = tcp_checksum(g_net.ip, c->peer_ip, buf, 20 + len);
    ip_send(c->peer_ip, 6, buf, 20 + len, (uint16_t)(c->local_port + len));
}

int tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
    if (!g_net.up) return -1;
    TcpConn* c = 0;
    for (int i = 0; i < MAX_TCPCONN; i++) {
        if (s_conns[i].state == TCP_CLOSED) { c = &s_conns[i]; break; }
    }
    if (!c) return -1;
    c->state = TCP_SYN_SENT;
    c->peer_ip = ip;
    c->peer_port = port;
    c->local_port = s_next_port++;
    c->snd_nxt = 0x1000;
    c->rcv_nxt = 0;
    c->rx_len = 0;
    c->rx_closed = false;
    int fd = (int)(c - s_conns);
    tcp_send_seg(c, 0x02, 0, 0, c->snd_nxt);   // SYN
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < (uint32_t)timeout_ms) {
        net_poll();
        if (c->state == TCP_ESTABLISHED) { g_net.tcp_conns++; return fd; }
        if (c->state == TCP_CLOSED) return -1;
    }
    c->state = TCP_CLOSED;
    return -1;
}

int tcp_send(int fd, const void* data, int len) {
    if (fd < 0 || fd >= MAX_TCPCONN) return -1;
    TcpConn* c = &s_conns[fd];
    if (c->state != TCP_ESTABLISHED) return -1;
    tcp_send_seg(c, 0x18, data, len, c->snd_nxt);   // PSH|ACK
    c->snd_nxt += (uint32_t)len;
    return len;
}

int tcp_recv(int fd, void* buf, int maxlen, int timeout_ms) {
    if (fd < 0 || fd >= MAX_TCPCONN) return -1;
    TcpConn* c = &s_conns[fd];
    uint32_t start = platform_tick_ms();
    while (c->rx_len == 0 && !c->rx_closed) {
        net_poll();
        if (platform_tick_ms() - start >= (uint32_t)timeout_ms) break;
    }
    if (c->rx_len == 0) return c->rx_closed ? 0 : -1;
    int n = c->rx_len;
    if (n > maxlen) n = maxlen;
    memcpy(buf, c->rxbuf, (size_t)n);
    if (n < c->rx_len) memmove(c->rxbuf, c->rxbuf + n, (size_t)(c->rx_len - n));
    c->rx_len -= n;
    return n;
}

void tcp_close(int fd) {
    if (fd < 0 || fd >= MAX_TCPCONN) return;
    TcpConn* c = &s_conns[fd];
    if (c->state == TCP_ESTABLISHED) {
        tcp_send_seg(c, 0x11, 0, 0, c->snd_nxt);   // FIN|ACK
        c->snd_nxt++;
        c->state = TCP_FIN_WAIT;
    }
    c->state = TCP_CLOSED;
    c->rx_len = 0;
}

static void tcp_handle(const uint8_t* ip_pkt, int iplen) {
    const IpPkt* ip = (const IpPkt*)ip_pkt;
    int hl = (ip->vihl & 0x0F) * 4;
    int total = __builtin_bswap16(ip->total_len);
    if (total < hl + 20 || total > iplen) return;
    const TcpHdr* t = (const TcpHdr*)(ip_pkt + hl);
    int tcp_len = total - hl;
    int data_len = tcp_len - 20;
    const uint8_t* data = ip_pkt + hl + 20;
    uint16_t srcp = __builtin_bswap16(t->src), dstp = __builtin_bswap16(t->dst);
    uint32_t pkt_src_ip = __builtin_bswap32(ip->src);
    TcpConn* c = 0;
    for (int i = 0; i < MAX_TCPCONN; i++) {
        if (s_conns[i].state != TCP_CLOSED && s_conns[i].local_port == dstp &&
            s_conns[i].peer_ip == pkt_src_ip && s_conns[i].peer_port == srcp) { c = &s_conns[i]; break; }
    }
    if (!c) return;
    uint8_t flags = t->flags;
    uint32_t seq = __builtin_bswap32(t->seq), ack = __builtin_bswap32(t->ack);
    if (flags & 0x10) {  // ACK
        // acknowledge our sent data (best effort; we don't track outstanding)
        (void)ack;
    }
    if (c->state == TCP_SYN_SENT) {
        if ((flags & 0x12) == 0x12) {  // SYN|ACK
            c->rcv_nxt = seq + 1;
            c->snd_nxt = ack;
            c->state = TCP_ESTABLISHED;
            tcp_send_seg(c, 0x10, 0, 0, c->snd_nxt);   // ACK
            return;
        }
        return;
    }
    if (flags & 0x02) {  // SYN (simultaneous or re-SYN): ack it
        c->rcv_nxt = seq + 1;
        tcp_send_seg(c, 0x10, 0, 0, c->snd_nxt);
        return;
    }
    if (data_len > 0 && (flags & 0x08)) {  // PSH data
        if (c->rx_len + data_len <= TCP_RXBUFSZ) {
            memcpy(c->rxbuf + c->rx_len, data, (size_t)data_len);
            c->rx_len += data_len;
        }
        c->rcv_nxt = seq + (uint32_t)data_len;
        tcp_send_seg(c, 0x10, 0, 0, c->snd_nxt);   // ACK
    } else if (data_len > 0) {
        c->rcv_nxt = seq + (uint32_t)data_len;
        tcp_send_seg(c, 0x10, 0, 0, c->snd_nxt);
    }
    if (flags & 0x01) {  // FIN
        c->rcv_nxt = seq + (uint32_t)data_len + 1;
        c->rx_closed = true;
        tcp_send_seg(c, 0x10, 0, 0, c->snd_nxt);
    }
}

// ---------------- RX dispatch ----------------
static void handle_frame(const uint8_t* frame, int len) {
    if (len < 14) return;
    const EthHdr* eh = (const EthHdr*)frame;
    uint16_t type = eh->type;
    if (type == ETH_ARP) arp_handle(frame, len);
    else if (type == ETH_IP) {
        if (len >= 34) {
            const IpPkt* ip = (const IpPkt*)(frame + 14);
            if (ip->vihl == 0x45 && ip->dst == __builtin_bswap32(g_net.ip)) {
                if (ip->proto == 1) icmp_handle(frame + 14, len - 14);
                else if (ip->proto == 6) tcp_handle(frame + 14, len - 14);
            }
        }
    }
}

void net_poll() {
    if (!s_mmio || !s_rx_desc) return;
    // NIC writes descriptors starting at RDH; software owns descs up to RDT.
    // Process (RDT+1 .. RDH) then hand them back by advancing RDT.
    uint32_t rdh = mmio32(REG_RDH);
    uint32_t cur = (mmio32(REG_RDT) + 1) % RX_RING;
    int processed = 0;
    while (processed < RX_RING && cur != rdh) {
        RxDesc* d = &s_rx_desc[cur];
        if (!(d->status & 0x01)) break;   // no DD yet
        int len = d->length;
        if (len > 0 && len <= RX_BUF) handle_frame(s_rx_buf[cur], len);
        d->status = 0;
        mmio_wr(REG_RDT, cur);
        g_net.rx_count++;
        cur = (cur + 1) % RX_RING;
        processed++;
    }
}

// ---------------- init ----------------
bool net_init() {
    memset(&g_net, 0, sizeof(g_net));
    // static config (QEMU user networking)
    g_net.ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;       // 10.0.2.15
    g_net.gw = (10u << 24) | (0u << 16) | (2u << 8) | 2u;        // 10.0.2.2
    g_net.netmask = (255u << 24) | (255u << 16) | (255u << 8) | 0u;

    // find e1000 (Intel 82540EM = 0x8086:0x100E)
    int found = -1;
    for (int dev = 0; dev < 32 && found < 0; dev++) {
        uint32_t vd = pci_read32(0, dev, 0, 0);
        if ((vd & 0xFFFF) == 0x8086 && ((vd >> 16) & 0xFFFF) == 0x100E) found = dev;
    }
    if (found < 0) { klogf("net: no e1000\n"); return false; }
    // enable PCI memory space + bus master, or the NIC cannot DMA any frame
    pci_write32(0, found, 0, 0x04, pci_read32(0, found, 0, 0x04) | 0x6u);
    uint32_t bar0 = pci_read32(0, found, 0, 0x10);
    bar0 &= ~0x0Fu;
    s_mmio = (volatile uint32_t*)(uintptr_t)bar0;

    // reset NIC
    mmio_wr(REG_CTRL, CTRL_RST);
    for (uint32_t i = 0; i < 2000000u; i++) io_wait_n();
    uint32_t ctrl = mmio32(REG_CTRL);
    ctrl |= CTRL_SLU | CTRL_ASDE;
    mmio_wr(REG_CTRL, ctrl);
    mmio_wr(REG_IMS, 0);
    (void)mmio32(REG_ICR);

    // MAC from registers
    uint32_t ral = mmio32(REG_RA0);
    uint32_t rah = mmio32(REG_RA1);
    g_net.mac[0] = (uint8_t)(ral & 0xFF);
    g_net.mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    g_net.mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    g_net.mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    g_net.mac[4] = (uint8_t)(rah & 0xFF);
    g_net.mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    if (!(g_net.mac[0] || g_net.mac[1] || g_net.mac[2])) {
        g_net.mac[0] = 0x52; g_net.mac[1] = 0x54; g_net.mac[2] = 0x00;
        g_net.mac[3] = 0x12; g_net.mac[4] = 0x34; g_net.mac[5] = 0x56;
    }

    // RX ring
    s_rx_desc = (RxDesc*)kalloc(RX_RING * sizeof(RxDesc) + 64);
    for (int i = 0; i < RX_RING; i++) {
        s_rx_buf[i] = (uint8_t*)kalloc(RX_BUF);
        memset(s_rx_buf[i], 0, RX_BUF);
        s_rx_desc[i].addr = (uint64_t)(uintptr_t)s_rx_buf[i];
        s_rx_desc[i].status = 0;
    }
    mmio_wr(REG_RDBAL, (uint32_t)((uintptr_t)s_rx_desc & 0xFFFFFFFFu));
    mmio_wr(REG_RDBAH, 0);
    mmio_wr(REG_RDLEN, RX_RING * (uint32_t)sizeof(RxDesc));
    mmio_wr(REG_RDH, 0);
    mmio_wr(REG_RDT, RX_RING - 1);

    // TX ring
    s_tx_desc = (TxDesc*)kalloc(TX_RING * sizeof(TxDesc) + 64);
    for (int i = 0; i < TX_RING; i++) {
        s_tx_buf[i] = (uint8_t*)kalloc(2048);
        s_tx_desc[i].addr = (uint64_t)(uintptr_t)s_tx_buf[i];
        s_tx_desc[i].status = 0;
    }
    mmio_wr(REG_TDBAL, (uint32_t)((uintptr_t)s_tx_desc & 0xFFFFFFFFu));
    mmio_wr(REG_TDBAH, 0);
    mmio_wr(REG_TDLEN, TX_RING * (uint32_t)sizeof(TxDesc));
    mmio_wr(REG_TDH, 0);
    mmio_wr(REG_TDT, 0);

    // enable (RCTL.EN=bit1, TCTL.EN=bit1)
    mmio_wr(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    mmio_wr(REG_TIPG, 0x0060200Au);
    mmio_wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_UPE | RCTL_MPE | RCTL_SECRC);

    // multicast table clear
    for (int i = 0; i < 128; i++) mmio_wr(REG_MTA + i * 4, 0);

    g_net.up = true;
    return true;
}

} // namespace nefu
