// nefuOS 网络协议栈 —— IPv4 实现
#include "ip.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ===================== 地址工具 =====================
bool ip_parse(const char* s, uint32_t& out) {
    if (!s) return false;
    uint32_t v = 0;
    int parts = 0;
    const char* p = s;
    while (parts < 4) {
        // 读一段十进制
        if (*p < '0' || *p > '9') return false;
        uint32_t oct = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            oct = oct * 10 + (uint32_t)(*p - '0');
            if (oct > 255) return false;
            p++;
            digits++;
        }
        if (digits == 0) return false;
        v = (v << 8) | oct;
        parts++;
        if (parts < 4) {
            if (*p != '.') return false;
            p++;
        }
    }
    // 末尾必须结束（不能有多余字符）
    if (*p != 0) return false;
    out = v;
    return true;
}

char* ip_format(uint32_t ip, char* buf, int bufsz) {
    unsigned a = (ip >> 24) & 0xFFu;
    unsigned b = (ip >> 16) & 0xFFu;
    unsigned c = (ip >> 8)  & 0xFFu;
    unsigned d = ip & 0xFFu;
    snprintf(buf, (size_t)bufsz, "%u.%u.%u.%u", a, b, c, d);
    return buf;
}

bool ip_is_multicast(uint32_t ip) {
    // 224.0.0.0/4 -> 首字节 224..239
    uint8_t first = (uint8_t)(ip >> 24);
    return first >= 224 && first <= 239;
}

bool ip_is_broadcast(uint32_t ip) { return ip == 0xFFFFFFFFu; }

bool ip_is_private(uint32_t ip) {
    uint8_t a = (uint8_t)(ip >> 24);
    uint8_t b = (uint8_t)(ip >> 16);
    if (a == 10) return true;                       // 10.0.0.0/8
    if (a == 172 && b >= 16 && b <= 31) return true; // 172.16/12
    if (a == 192 && b == 168) return true;          // 192.168/16
    return false;
}

bool ip_is_loopback(uint32_t ip) {
    return (ip >> 24) == 127u;   // 127.0.0.0/8
}

bool ip_in_subnet(uint32_t ip, uint32_t net, uint32_t mask) {
    return (ip & mask) == (net & mask);
}

// ===================== CIDR =====================
uint32_t ip_prefix_to_mask(int prefix) {
    if (prefix <= 0) return 0;
    if (prefix >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefix);
}

int ip_mask_to_prefix(uint32_t mask) {
    int n = 0;
    uint32_t m = mask;
    // 数前导 1
    for (int i = 31; i >= 0; i--) {
        if (m & (1u << i)) n++;
        else break;
    }
    return n;
}

int ip_parse_cidr(const char* s, uint32_t& net, uint32_t& mask) {
    if (!s) return -1;
    // 找到 '/'
    const char* slash = strchr(s, '/');
    if (!slash) return -1;
    int hostlen = (int)(slash - s);
    // 临时把 "a.b.c.d" 拷出来解析
    char buf[24];
    int n = hostlen; if (n > 23) n = 23;
    for (int i = 0; i < n; i++) buf[i] = s[i];
    buf[n] = 0;
    uint32_t addr = 0;
    if (!ip_parse(buf, addr)) return -1;
    int prefix = 0;
    slash++;
    while (*slash >= '0' && *slash <= '9') {
        prefix = prefix * 10 + (*slash - '0');
        slash++;
    }
    if (prefix < 0 || prefix > 32) return -1;
    mask = ip_prefix_to_mask(prefix);
    net = addr & mask;
    return 0;
}

// ===================== 路由表 =====================
int RouteTable::add(uint32_t net, uint32_t mask, uint32_t gateway, int metric, const char* iface) {
    RouteEntry e;
    e.net = net & mask;
    e.mask = mask;
    e.gateway = gateway;
    e.prefix = ip_mask_to_prefix(mask);
    e.metric = metric;
    int i = 0;
    if (iface) {
        while (iface[i] && i < 7) { e.iface[i] = iface[i]; i++; }
    }
    e.iface[i] = 0;
    routes_.push(e);
    return routes_.size();
}

int RouteTable::add_cidr(const char* cidr, uint32_t gateway, const char* iface) {
    uint32_t net, mask;
    if (ip_parse_cidr(cidr, net, mask) != 0) return -1;
    return add(net, mask, gateway, 0, iface);
}

bool RouteTable::lookup(uint32_t dst, RouteEntry& out) const {
    int best = -1;
    int best_prefix = -1;
    for (int i = 0; i < routes_.size(); i++) {
        const RouteEntry& e = routes_[i];
        if ((dst & e.mask) == e.net) {
            // 最长前缀优先；同前缀时 metric 小者优先
            if (e.prefix > best_prefix ||
                (e.prefix == best_prefix && best >= 0 && e.metric < routes_[best].metric)) {
                best = i;
                best_prefix = e.prefix;
            }
        }
    }
    if (best < 0) return false;
    out = routes_[best];
    return true;
}

// ===================== 头部 =====================
int ip_pack_header(uint8_t* out, uint16_t ident, uint8_t ttl, uint8_t protocol,
                   uint32_t src, uint32_t dst, uint16_t total_len,
                   uint16_t flags_fragoff) {
    if (!out) return -1;
    out[0] = 0x45;                 // version=4, IHL=5 (20 字节)
    out[1] = 0;                    // TOS
    put_be16(out + 2, total_len);
    put_be16(out + 4, ident);
    put_be16(out + 6, flags_fragoff);
    out[8] = ttl;
    out[9] = protocol;
    put_be16(out + 10, 0);         // 校验和先置 0
    put_be32(out + 12, src);
    put_be32(out + 16, dst);
    uint16_t cksum = ip_header_checksum(out, IP_HDR_LEN);
    put_be16(out + 10, cksum);
    return IP_HDR_LEN;
}

int ip_parse_header(const uint8_t* pkt, int len, IpHeader& out, bool verify_csum) {
    if (!pkt || len < IP_HDR_LEN) return -1;
    if ((pkt[0] >> 4) != 4) return -1;             // 只处理 IPv4
    out.version = pkt[0] >> 4;
    out.ihl = (pkt[0] & 0x0F) * 4;                  // IHL 单位是 4 字节
    if (out.ihl < IP_HDR_LEN || len < out.ihl) return -1;
    out.tos = pkt[1];
    out.total_len = be16(pkt + 2);
    out.ident = be16(pkt + 4);
    out.flags = be16(pkt + 6);
    out.ttl = pkt[8];
    out.protocol = pkt[9];
    out.src = be32(pkt + 12);
    out.dst = be32(pkt + 16);
    if (verify_csum) {
        // 对整包头部（含校验和字段）求和，结果应为 0
        uint16_t c = csum_fold(csum_add(0, pkt, out.ihl));
        if (c != 0) return -2;   // 校验和错误
    }
    return out.ihl;
}

uint16_t ip_header_checksum(const uint8_t* pkt, int hdr_len) {
    // 跳过校验和字段自身（字节 10..11），其余 16 位字求和取反
    uint32_t sum = 0;
    int i = 0;
    while (i + 1 < hdr_len) {
        if (i != 10) {
            sum += be16(pkt + i);
            if (sum & 0x10000u) sum = (sum & 0xFFFFu) + (sum >> 16);
        }
        i += 2;
    }
    // 奇数长度补一个零字节
    if (i < hdr_len) sum += (uint32_t)pkt[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

const char* ip_proto_name(uint8_t p) {
    switch (p) {
    case IPPROTO_ICMP: return "ICMP";
    case IPPROTO_TCP:  return "TCP";
    case IPPROTO_UDP:  return "UDP";
    default:           return "?";
    }
}

int ip_forward_dec_ttl(uint8_t* pkt, int len) {
    if (!pkt || len < IP_HDR_LEN) return -1;
    uint8_t ttl = pkt[8];
    if (ttl <= 1) return -1;
    pkt[8] = (uint8_t)(ttl - 1);
    pkt[10] = 0; pkt[11] = 0;
    uint16_t c = csum_of(pkt, IP_HDR_LEN);
    pkt[10] = (uint8_t)(c >> 8);
    pkt[11] = (uint8_t)(c & 0xFFu);
    return pkt[8];
}

// ===================== 分片 =====================
int ip_fragment(const uint8_t* full, int full_len, int mtu, List<IpFrag>& frags) {
    IpHeader h;
    int hlen = ip_parse_header(full, full_len, h, false);
    if (hlen < 0) return -1;
    if (mtu <= hlen + 8) return -1;

    // 每个分片最多承载的 payload 字节（必须是 8 的倍数，最后一片除外）
    int max_payload = ((mtu - hlen) / 8) * 8;
    if (max_payload < 8) return -1;

    const uint8_t* payload = full + hlen;
    int total_payload = h.total_len - hlen;
    if (total_payload < 0) return -1;

    int offset = 0;      // 字节偏移
    int remaining = total_payload;
    int count = 0;
    while (remaining > 0) {
        int chunk = remaining;
        bool last = (chunk <= max_payload);
        if (!last) chunk = max_payload;

        int frag_len = hlen + chunk;
        uint8_t* buf = new uint8_t[frag_len];
        if (!buf) { ip_frags_free(frags); return -1; }
        // 拷贝原 IP 头
        np_copy(buf, full, hlen);
        // 拷贝这一段 payload
        np_copy(buf + hlen, payload + offset, chunk);
        // 调整总长
        put_be16(buf + 2, (uint16_t)frag_len);
        // 设置标志 + 偏移：保留原 DF，按需要置 MF，偏移单位 8 字节
        uint16_t ffo = (h.flags & IP_FLAG_DF) | (uint16_t)(offset / 8);
        if (!last) ffo |= IP_FLAG_MF;
        put_be16(buf + 6, ffo);
        // 重算校验和
        put_be16(buf + 10, 0);
        put_be16(buf + 10, ip_header_checksum(buf, hlen));

        IpFrag f; f.data = buf; f.len = frag_len;
        frags.push(f);
        count++;

        offset += chunk;
        remaining -= chunk;
    }
    if (count == 0) {
        // 零 payload 的异常包：仍产出一个完整包
        uint8_t* buf = new uint8_t[hlen];
        np_copy(buf, full, hlen);
        put_be16(buf + 6, h.flags & ~IP_FLAG_MF);
        put_be16(buf + 10, 0);
        put_be16(buf + 10, ip_header_checksum(buf, hlen));
        IpFrag f; f.data = buf; f.len = hlen;
        frags.push(f);
        count = 1;
    }
    return count;
}

void ip_frags_free(List<IpFrag>& frags) {
    for (int i = 0; i < frags.size(); i++) {
        if (frags[i].data) delete[] frags[i].data;
        frags[i].data = 0;
    }
    frags.clear();
}

// ===================== 重组 =====================
IpReassembler::IpReassembler() : groups_() {}
IpReassembler::~IpReassembler() { clear(); }

int IpReassembler::find_group(uint16_t ident, uint32_t src, uint32_t dst, uint8_t proto) {
    for (int i = 0; i < groups_.size(); i++) {
        Group& g = groups_[i];
        if (g.ident == ident && g.src == src && g.dst == dst && g.proto == proto) return i;
    }
    return -1;
}

void IpReassembler::free_group(Group& g) {
    for (int i = 0; i < g.blocks.size(); i++) {
        if (g.blocks[i].data) delete[] g.blocks[i].data;
        g.blocks[i].data = 0;
    }
    g.blocks.clear();
}

int IpReassembler::add(const uint8_t* pkt, int len, uint8_t** out) {
    *out = 0;
    IpHeader h;
    int hlen = ip_parse_header(pkt, len, h, true);
    if (hlen < 0) return -1;

    int paylen = h.total_len - hlen;
    if (paylen < 0) return -1;
    int fragoff = (h.flags & 0x1FFFu) * 8;   // 低 13 位，单位 8 字节
    bool mf = (h.flags & IP_FLAG_MF) != 0;

    int gi = find_group(h.ident, h.src, h.dst, h.protocol);
    if (gi < 0) {
        Group g;
        g.ident = h.ident; g.src = h.src; g.dst = h.dst; g.proto = h.protocol;
        g.total = -1; g.base = h;
        groups_.push(g);
        gi = groups_.size() - 1;
    }
    Group& g = groups_[gi];

    Block b;
    b.off = fragoff;
    b.len = paylen;
    b.data = new uint8_t[paylen > 0 ? paylen : 1];
    if (paylen > 0) np_copy(b.data, pkt + hlen, paylen);
    g.blocks.push(b);

    if (!mf) g.total = fragoff + paylen;

    if (g.total < 0) return 0;   // 还没收到尾片，继续等

    // 按偏移升序排序（简单插入排序，分片数很少）
    for (int i = 1; i < g.blocks.size(); i++) {
        Block key = g.blocks[i];
        int j = i - 1;
        while (j >= 0 && g.blocks[j].off > key.off) {
            g.blocks[j + 1] = g.blocks[j];
            j--;
        }
        g.blocks[j + 1] = key;
    }

    // 检查覆盖 [0, total)
    int next = 0;
    bool contiguous = true;
    for (int i = 0; i < g.blocks.size(); i++) {
        Block& blk = g.blocks[i];
        if (blk.off > next) { contiguous = false; break; }   // 有洞
        if (blk.off + blk.len > next) next = blk.off + blk.len;
    }
    if (!contiguous || next < g.total) return 0;   // 还没集齐

    // 组装：头 + 全部 payload
    int hdr = hlen;
    int total = hdr + g.total;
    uint8_t* assembled = new uint8_t[total];
    np_zero(assembled, total);
    // 写入所有分片数据
    for (int i = 0; i < g.blocks.size(); i++) {
        Block& blk = g.blocks[i];
        if (blk.len > 0) np_copy(assembled + hdr + blk.off, blk.data, blk.len);
    }
    // 构造新的完整 IP 头：MF=0，偏移=0，total_len=total
    np_copy(assembled, pkt, hdr);
    put_be16(assembled + 2, (uint16_t)total);
    put_be16(assembled + 6, (h.flags & IP_FLAG_DF));   // 清 MF / 偏移
    put_be16(assembled + 10, 0);
    put_be16(assembled + 10, ip_header_checksum(assembled, hdr));

    // 释放该组并从列表移除
    free_group(g);
    groups_.remove(gi);
    *out = assembled;
    return total;
}

void IpReassembler::clear() {
    for (int i = 0; i < groups_.size(); i++) free_group(groups_[i]);
    groups_.clear();
}

int IpReassembler::group_count() const { return groups_.size(); }

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [ip] FAIL: %s\n", what); }
}
} // namespace

int ip_self_test() {
    g_fails = 0;

    // 1) 地址解析 / 格式化
    uint32_t ip;
    expect("ip parse", ip_parse("192.168.1.1", ip) && ip == 0xC0A80101u);
    expect("ip parse 8.8.8.8", ip_parse("8.8.8.8", ip) && ip == 0x08080808u);
    expect("ip parse bad", !ip_parse("256.1.1.1", ip));
    expect("ip parse short", !ip_parse("1.2.3", ip));
    char ipb[20];
    ip_format(0xC0A80101u, ipb, sizeof(ipb));
    expect("ip format", strcmp(ipb, "192.168.1.1") == 0);

    // 2) 地址分类
    expect("multicast", ip_is_multicast(0xE0000001u));   // 224.0.0.1
    expect("broadcast", ip_is_broadcast(0xFFFFFFFFu));
    expect("private 192.168", ip_is_private(0xC0A80101u));
    expect("private 10", ip_is_private(0x0A000001u));
    expect("public not private", !ip_is_private(0x08080808u));
    expect("loopback", ip_is_loopback(0x7F000001u));
    expect("subnet", ip_in_subnet(0xC0A80150u, 0xC0A80100u, 0xFFFFFF00u));
    expect("subnet reject", !ip_in_subnet(0xC0A80250u, 0xC0A80100u, 0xFFFFFF00u));

    // 3) 头部封装 + 校验和：构造一个 20 字节头，解析回来，校验和通过
    uint8_t hdr[IP_HDR_LEN];
    uint32_t src = 0xC0A8010Au, dst = 0x08080808u;
    int hl = ip_pack_header(hdr, 0x1234, 64, IPPROTO_ICMP, src, dst, 40, 0);
    expect("ip pack len", hl == IP_HDR_LEN);
    // 整包校验和折叠应为 0
    expect("ip csum valid", csum_fold(csum_add(0, hdr, IP_HDR_LEN)) == 0);

    IpHeader ph;
    int r = ip_parse_header(hdr, IP_HDR_LEN, ph, true);
    expect("ip parse", r == IP_HDR_LEN);
    expect("ip parse proto", ph.protocol == IPPROTO_ICMP);
    expect("ip parse src", ph.src == src && ph.dst == dst);
    expect("ip parse ident", ph.ident == 0x1234);
    expect("ip parse ttl", ph.ttl == 64);

    // 破坏一个字节，校验和应失败
    uint8_t bad[IP_HDR_LEN];
    np_copy(bad, hdr, IP_HDR_LEN);
    bad[12] ^= 0xFF;
    expect("ip csum detect", ip_parse_header(bad, IP_HDR_LEN, ph, true) < 0);

    // 4) 分片 + 重组：构造一个 1400 字节 payload 的 ICMP 包，按 MTU=576 分片
    {
        const int PAYLOAD = 1400;
        int full_len = IP_HDR_LEN + PAYLOAD;
        uint8_t* full = new uint8_t[full_len];
        np_zero(full, full_len);
        ip_pack_header(full, 0xBEEF, 64, IPPROTO_UDP, src, dst, (uint16_t)full_len, 0);
        // 填充一个确定性 payload
        for (int i = 0; i < PAYLOAD; i++) full[IP_HDR_LEN + i] = (uint8_t)(i * 31 + 7);

        List<IpFrag> frags;
        int nf = ip_fragment(full, full_len, 576, frags);
        expect("frag count", nf >= 3);
        // 每个分片都不超过 MTU
        bool ok = true;
        for (int i = 0; i < frags.size(); i++) if (frags[i].len > 576) ok = false;
        expect("frag <= mtu", ok);
        // 除最后一片外 MF 都应置位
        for (int i = 0; i < frags.size(); i++) {
            IpHeader fh;
            ip_parse_header(frags[i].data, frags[i].len, fh, true);
            bool expect_mf = (i < frags.size() - 1);
            if (((fh.flags & IP_FLAG_MF) != 0) != expect_mf) ok = false;
        }
        expect("frag mf flags", ok);

        // 乱序喂给重组器
        IpReassembler reasm;
        uint8_t* out = 0;
        int got = 0;
        // 反序喂
        for (int i = frags.size() - 1; i >= 0; i--) {
            got = reasm.add(frags[i].data, frags[i].len, &out);
        }
        expect("reasm complete", got == full_len && out != 0);
        if (out) {
            // 比较重组结果与原始整包
            bool eq = true;
            for (int i = 0; i < full_len && eq; i++) {
                // 重组包头的分片标志位应被清零、总长修正——逐字节比 payload 部分
                if (i >= IP_HDR_LEN && out[i] != full[i]) eq = false;
            }
            expect("reasm payload match", eq);
            // 重组后的包应能通过校验和且总长正确
            IpHeader rh;
            int rhl = ip_parse_header(out, got, rh, true);
            expect("reasm hdr valid", rhl > 0 && rh.total_len == (uint16_t)full_len);
            expect("reasm no mf", (rh.flags & IP_FLAG_MF) == 0);
            delete[] out;
        }
        ip_frags_free(frags);
        delete[] full;
    }

    // 5) 名字
    expect("proto name", strcmp(ip_proto_name(IPPROTO_TCP), "TCP") == 0);

    // 6) CIDR
    expect("mask /24", ip_prefix_to_mask(24) == 0xFFFFFF00u);
    expect("mask /0", ip_prefix_to_mask(0) == 0u);
    expect("mask /32", ip_prefix_to_mask(32) == 0xFFFFFFFFu);
    expect("prefix from mask", ip_mask_to_prefix(0xFFFFFF00u) == 24);
    uint32_t net, mask;
    expect("cidr parse", ip_parse_cidr("192.168.1.0/24", net, mask) == 0);
    expect("cidr net", net == 0xC0A80100u);
    expect("cidr mask", mask == 0xFFFFFF00u);

    // 7) 路由表最长前缀匹配
    RouteTable rt;
    rt.add_cidr("0.0.0.0/0", 0xC0A80101u, "eth0");        // 默认路由 -> 网关 .1
    rt.add_cidr("192.168.1.0/24", 0, "eth0");            // 直连
    rt.add_cidr("10.0.0.0/8", 0x0A000001u, "wlan0");
    RouteEntry e;
    // 192.168.1.50 应命中 /24 直连
    expect("rt subnet", rt.lookup(0xC0A80132u, e) && e.prefix == 24 && e.gateway == 0);
    // 8.8.8.8 应命中默认路由
    expect("rt default", rt.lookup(0x08080808u, e) && e.prefix == 0 &&
           e.gateway == 0xC0A80101u);
    // 10.1.2.3 应命中 /8
    expect("rt ten", rt.lookup(0x0A010203u, e) && e.prefix == 8 &&
           strcmp(e.iface, "wlan0") == 0);

    // 8) TTL 递减转发
    uint8_t pk[20];
    ip_pack_header(pk, 1, 64, IPPROTO_TCP, 0x0A000001u, 0x08080808u, 20, 0);
    int nt = ip_forward_dec_ttl(pk, 20);
    expect("ttl decremented", nt == 63 && pk[8] == 63);
    // 重算后校验和仍合法（对整头求和应为 0）
    expect("ttl csum ok", csum_of(pk, 20) == 0);
    // TTL=1 应返回 -1
    pk[8] = 1;
    expect("ttl expire", ip_forward_dec_ttl(pk, 20) == -1);

    // 9) 地址分类
    expect("loopback", ip_is_loopback(0x7F000001u));
    expect("multicast", ip_is_multicast(0xE0000001u));
    expect("broadcast", ip_is_broadcast(0xFFFFFFFFu));
    expect("private 10", ip_is_private(0x0A000001u));
    expect("private 192", ip_is_private(0xC0A80101u));
    expect("public 8.8", !ip_is_private(0x08080808u));

    // 10) 分片标志
    expect("df", ip_df_set(0x4000u));
    expect("mf", ip_mf_set(0x2000u));
    expect("offset", ip_frag_offset(0x2037u) == 0x0037u);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
