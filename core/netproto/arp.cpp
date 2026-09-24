// nefuOS 网络协议栈 —— ARP 实现
#include "arp.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// 把主机序 IPv4 格式成 "a.b.c.d"（ARP 缓存调试用）。
static int fmt_ip(uint32_t ip, char* buf, int bufsz) {
    unsigned a = (ip >> 24) & 0xFFu;
    unsigned b = (ip >> 16) & 0xFFu;
    unsigned c = (ip >> 8)  & 0xFFu;
    unsigned d = (ip)       & 0xFFu;
    return snprintf(buf, (size_t)bufsz, "%u.%u.%u.%u", a, b, c, d);
}

// 内部：把 ArpPacket 打包成 28 字节线序。
static int arp_pack(const ArpPacket& p, uint8_t* out) {
    put_be16(out + 0, p.htype);
    put_be16(out + 2, p.ptype);
    out[4] = p.hlen;
    out[5] = p.plen;
    put_be16(out + 6, p.oper);
    for (int i = 0; i < 6; i++) out[8 + i] = p.sha.oct[i];
    put_be32(out + 14, p.spa);
    for (int i = 0; i < 6; i++) out[18 + i] = p.tha.oct[i];
    put_be32(out + 24, p.tpa);
    return ARP_HDR_LEN;
}

int arp_build_request(uint8_t* out, uint32_t sender_ip, const MacAddr& sender_mac,
                      uint32_t target_ip) {
    ArpPacket p;
    np_zero(&p, sizeof(p));
    p.htype = 1;             // 以太网
    p.ptype = ET_IPV4;       // IPv4
    p.hlen = 6;
    p.plen = 4;
    p.oper = ARP_OP_REQUEST;
    p.sha = sender_mac;
    p.spa = sender_ip;
    np_zero(p.tha.oct, 6);   // 请求里目标 MAC 未知，全零
    p.tpa = target_ip;
    return arp_pack(p, out);
}

int arp_build_reply(uint8_t* out, const MacAddr& sender_mac, uint32_t sender_ip,
                    const MacAddr& target_mac, uint32_t target_ip) {
    ArpPacket p;
    np_zero(&p, sizeof(p));
    p.htype = 1;
    p.ptype = ET_IPV4;
    p.hlen = 6;
    p.plen = 4;
    p.oper = ARP_OP_REPLY;
    p.sha = sender_mac;
    p.spa = sender_ip;
    p.tha = target_mac;
    p.tpa = target_ip;
    return arp_pack(p, out);
}

int arp_build_gratuitous(uint8_t* out, const MacAddr& my_mac, uint32_t my_ip) {
    ArpPacket p;
    np_zero(&p, sizeof(p));
    p.htype = 1;
    p.ptype = ET_IPV4;
    p.hlen = 6;
    p.plen = 4;
    p.oper = ARP_OP_REQUEST;
    p.sha = my_mac;
    p.spa = my_ip;
    np_zero(p.tha.oct, 6);   // 目标 MAC 全零
    p.tpa = my_ip;           // 目标 IP = 自己
    return arp_pack(p, out);
}

const char* arp_op_name(uint16_t op) {
    if (op == ARP_OP_REQUEST) return "Request";
    if (op == ARP_OP_REPLY)   return "Reply";
    return "?";
}

int arp_parse(const uint8_t* pkt, int len, ArpPacket& out) {
    if (!pkt || len < ARP_HDR_LEN) return -1;
    out.htype = be16(pkt + 0);
    out.ptype = be16(pkt + 2);
    out.hlen  = pkt[4];
    out.plen  = pkt[5];
    out.oper  = be16(pkt + 6);
    for (int i = 0; i < 6; i++) out.sha.oct[i] = pkt[8 + i];
    out.spa = be32(pkt + 14);
    for (int i = 0; i < 6; i++) out.tha.oct[i] = pkt[18 + i];
    out.tpa = be32(pkt + 24);
    return 0;
}

// ===================== ARP 缓存 =====================
void ArpCache::add(uint32_t ip, const MacAddr& mac, uint32_t now_ms, uint32_t ttl_ms) {
    if (ttl_ms == 0) ttl_ms = 30000;
    // 已存在则刷新
    for (int i = 0; i < entries_.size(); i++) {
        if (entries_[i].ip == ip) {
            entries_[i].mac = mac;
            entries_[i].expire_ms = now_ms + ttl_ms;
            entries_[i].stale = false;
            return;
        }
    }
    ArpEntry e;
    e.ip = ip;
    e.mac = mac;
    e.expire_ms = now_ms + ttl_ms;
    e.stale = false;
    entries_.push(e);
}

bool ArpCache::lookup(uint32_t ip, MacAddr& out_mac, uint32_t now_ms) {
    for (int i = 0; i < entries_.size(); i++) {
        ArpEntry& e = entries_[i];
        if (e.ip == ip) {
            if (now_ms >= e.expire_ms) { e.stale = true; return false; }
            out_mac = e.mac;
            return true;
        }
    }
    return false;
}

int ArpCache::expire(uint32_t now_ms) {
    // 从后往前删除，避免下标错位
    for (int i = entries_.size() - 1; i >= 0; i--) {
        if (now_ms >= entries_[i].expire_ms) entries_.remove(i);
    }
    return entries_.size();
}

void ArpCache::format_entry(int i, char* buf, int bufsz) const {
    if (i < 0 || i >= entries_.size()) {
        if (bufsz > 0) buf[0] = 0;
        return;
    }
    const ArpEntry& e = entries_[i];
    char ipbuf[24];
    char macbuf[24];
    fmt_ip(e.ip, ipbuf, sizeof(ipbuf));
    mac_format(e.mac, macbuf, sizeof(macbuf));
    snprintf(buf, (size_t)bufsz, "%-15s -> %s%s", ipbuf, macbuf,
             e.stale ? " (stale)" : "");
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [arp] FAIL: %s\n", what); }
}
} // namespace

int arp_self_test() {
    g_fails = 0;
    uint8_t buf[64];
    ArpPacket p;

    // 1) 构造一个请求并检查字节
    MacAddr my_mac; mac_parse("52:54:00:AB:CD:EF", my_mac);
    uint32_t me = (192u << 24) | (168u << 16) | (1u << 8) | 10u;   // 192.168.1.10
    uint32_t gw = (192u << 24) | (168u << 16) | (1u << 8) | 1u;    // 192.168.1.1
    int n = arp_build_request(buf, me, my_mac, gw);
    expect("arp req len", n == ARP_HDR_LEN);
    expect("arp req htype", be16(buf + 0) == 1);
    expect("arp req ptype", be16(buf + 2) == 0x0800);
    expect("arp req hlen", buf[4] == 6 && buf[5] == 4);
    expect("arp req oper", be16(buf + 6) == ARP_OP_REQUEST);
    expect("arp req spa", be32(buf + 14) == me);
    expect("arp req tpa", be32(buf + 24) == gw);
    expect("arp req tha zero", buf[18] == 0 && buf[23] == 0);

    // 2) 解析回读请求
    int r = arp_parse(buf, n, p);
    expect("arp parse req", r == 0 && p.oper == ARP_OP_REQUEST);
    expect("arp parse sha", p.sha == my_mac);
    expect("arp parse spa", p.spa == me && p.tpa == gw);

    // 3) 构造应答并解析
    MacAddr gw_mac; mac_parse("00:11:22:33:44:55", gw_mac);
    n = arp_build_reply(buf, gw_mac, gw, my_mac, me);
    expect("arp reply len", n == ARP_HDR_LEN);
    r = arp_parse(buf, n, p);
    expect("arp parse reply oper", r == 0 && p.oper == ARP_OP_REPLY);
    expect("arp reply sha", p.sha == gw_mac);
    expect("arp reply tha", p.tha == my_mac);

    // 4) 太短拒绝
    expect("arp too short", arp_parse(buf, 10, p) == -1);

    // 5) ARP 缓存：插入 / 查找 / 刷新 / 过期
    ArpCache cache;
    uint32_t now = 1000;
    cache.add(gw, gw_mac, now, 5000);          // 5 秒 TTL
    MacAddr got;
    expect("cache hit", cache.lookup(gw, got, now + 1000) && got == gw_mac);
    expect("cache miss unknown", !cache.lookup(me, got, now + 1000));
    expect("cache size 1", cache.size() == 1);

    // 未过期前不淘汰
    expect("cache expire keep", cache.expire(now + 2000) == 1);

    // 过期后查找失败并标记 stale
    bool hit = cache.lookup(gw, got, now + 99999);
    expect("cache expired miss", !hit);

    // 淘汰
    expect("cache pruned", cache.expire(now + 99999) == 0);

    // 刷新：重新 add 同一个 IP，不应产生重复项
    cache.add(gw, gw_mac, now, 5000);
    cache.add(gw, gw_mac, now, 5000);
    expect("cache dedup", cache.size() == 1);

    // 格式化一条记录
    char fbuf[64];
    cache.format_entry(0, fbuf, sizeof(fbuf));
    expect("cache format", strstr(fbuf, "192.168.1.1") != 0);

    // 6) 免费 ARP：spa == tpa，tha 全零，oper=request
    n = arp_build_gratuitous(buf, my_mac, me);
    expect("garp len", n == ARP_HDR_LEN);
    r = arp_parse(buf, n, p);
    expect("garp oper", r == 0 && p.oper == ARP_OP_REQUEST);
    expect("garp spa==tpa", p.spa == me && p.tpa == me);
    expect("garp tha zero", p.tha.is_zero());


    expect("arp op name", strcmp(arp_op_name(ARP_OP_REQUEST), "Request") == 0 &&
           strcmp(arp_op_name(ARP_OP_REPLY), "Reply") == 0);
    return g_fails;
}

} // namespace netproto
} // namespace nefu
