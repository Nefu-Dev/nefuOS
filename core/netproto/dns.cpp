// nefuOS 网络协议栈 —— DNS 实现
#include "dns.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// 把 "www.example.com" 编码成标签序列写入 out，返回字节数。
// 形式：3'w'w'w' 7'e'x'a'm'p'l'e' 3'c'o'm' 0
static int encode_name(uint8_t* out, const char* name) {
    int o = 0;
    const char* p = name;
    while (true) {
        // 找一段标签
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int labellen = (int)(dot - p);
        if (labellen > 63) labellen = 63;
        out[o++] = (uint8_t)labellen;
        for (int i = 0; i < labellen; i++) out[o++] = (uint8_t)p[i];
        if (*dot == 0) break;
        p = dot + 1;   // 跳过 '.'
    }
    out[o++] = 0;     // 结束
    return o;
}

int dns_build_query(uint8_t* out, uint16_t id, const char* name, uint16_t qtype) {
    if (!out || !name) return -1;
    put_be16(out + 0, id);
    put_be16(out + 2, 0x0100);   // 标准查询，RD=1
    put_be16(out + 4, 1);        // QDCOUNT=1
    put_be16(out + 6, 0);
    put_be16(out + 8, 0);
    put_be16(out + 10, 0);
    int o = 12;
    o += encode_name(out + o, name);
    put_be16(out + o, qtype); o += 2;
    put_be16(out + o, DNS_CLASS_IN); o += 2;
    return o;
}

// 从 pkt 的 off 处读一个（可能被压缩的）域名到 out。
// *next_off 返回名字之后第一个字节的偏移。成功返回 0。
static int read_name(const uint8_t* pkt, int len, int off, int* next_off, String& out) {
    out = "";
    int jumped_pos = -1;     // 第一次遇到指针后，真正的"下一个偏移"
    int steps = 0;
    while (true) {
        if (off >= len || steps++ > 127) return -1;
        uint8_t b = pkt[off];
        if (b == 0) {
            off++;
            if (jumped_pos < 0) *next_off = off;
            else *next_off = jumped_pos;
            return 0;
        }
        if ((b & 0xC0) == 0xC0) {
            // 14 位指针
            if (off + 1 >= len) return -1;
            uint16_t ptr = (uint16_t)((b & 0x3Fu) << 8) | pkt[off + 1];
            if (jumped_pos < 0) jumped_pos = off + 2;
            off = ptr;
            continue;
        }
        // 普通标签
        int labellen = b;
        off++;
        if (off + labellen > len) return -1;
        if (out.len() > 0) out += '.';
        for (int i = 0; i < labellen; i++) out += (char)pkt[off + i];
        off += labellen;
    }
}

// 读一条资源记录的 rdata 部分（按类型解释）
static void parse_rdata(DnsRecord& rec, const uint8_t* pkt, int len, int rdata_off, int rdlength) {
    if (rec.rtype == DNS_TYPE_A && rdlength == 4) {
        rec.a_ip = be32(pkt + rdata_off);
    } else if (rec.rtype == DNS_TYPE_CNAME || rec.rtype == DNS_TYPE_NS) {
        int no = 0;
        read_name(pkt, len, rdata_off, &no, rec.cname);
    } else if (rec.rtype == DNS_TYPE_MX && rdlength >= 3) {
        rec.mx_pref = be16(pkt + rdata_off);
        int no = 0;
        read_name(pkt, len, rdata_off + 2, &no, rec.cname);
    } else if (rec.rtype == DNS_TYPE_TXT) {
        // TXT：一段或多段长度前缀字符串，拼接起来
        int p = rdata_off;
        int end = rdata_off + rdlength;
        while (p < end) {
            uint8_t slen = pkt[p];
            p++;
            for (int i = 0; i < slen && p < end; i++) rec.cname += (char)pkt[p++];
        }
        // TXT 内容直接放在 cname 字段（复用文本槽）
    }
}

int dns_parse(const uint8_t* pkt, int len, DnsMessage& out) {
    if (!pkt || len < 12) return -1;
    out.clear();
    out.id      = be16(pkt + 0);
    out.flags   = be16(pkt + 2);
    out.qdcount = be16(pkt + 4);
    out.ancount = be16(pkt + 6);
    out.nscount = be16(pkt + 8);
    out.arcount = be16(pkt + 10);
    out.qr    = (out.flags & 0x8000u) != 0;
    out.rcode = (uint8_t)(out.flags & 0x000Fu);

    int off = 12;

    // 问题段
    for (int i = 0; i < out.qdcount; i++) {
        DnsQuestion q;
        int no = 0;
        if (read_name(pkt, len, off, &no, q.qname) != 0) return -1;
        off = no;
        if (off + 4 > len) return -1;
        q.qtype  = be16(pkt + off); off += 2;
        q.qclass = be16(pkt + off); off += 2;
        out.questions.push(q);
    }

    // 回答段
    for (int i = 0; i < out.ancount; i++) {
        DnsRecord rec;
        int no = 0;
        if (read_name(pkt, len, off, &no, rec.name) != 0) return -1;
        off = no;
        if (off + 10 > len) return -1;
        rec.rtype  = be16(pkt + off); off += 2;
        off += 2;                       // class（跳过，恒为 IN）
        rec.ttl    = be32(pkt + off); off += 4;
        uint16_t rdlength = be16(pkt + off); off += 2;
        if (off + rdlength > len) return -1;
        parse_rdata(rec, pkt, len, off, rdlength);
        off += rdlength;
        out.answers.push(rec);
    }
    return 0;
}

const char* dns_type_name(uint16_t t) {
    switch (t) {
    case DNS_TYPE_A:     return "A";
    case DNS_TYPE_NS:    return "NS";
    case DNS_TYPE_CNAME: return "CNAME";
    case DNS_TYPE_MX:    return "MX";
    case DNS_TYPE_TXT:   return "TXT";
    case DNS_TYPE_AAAA:  return "AAAA";
    default:             return "?";
    }
}

const char* dns_class_name(uint16_t c) {
    if (c == DNS_CLASS_IN) return "IN";
    return "?";
}

bool dns_is_truncated(const DnsMessage& m) { return (m.flags & 0x0200u) != 0; }
bool dns_is_nxdomain(const DnsMessage& m) { return (m.flags & 0x000Fu) == 3; }

int dns_count_by_type(const DnsMessage& m, uint16_t rtype) {
    int n = 0;
    for (int i = 0; i < m.answers.size(); i++)
        if (m.answers[i].rtype == rtype) n++;
    return n;
}

bool dns_first_a(const DnsMessage& m, uint32_t& out_ip) {
    for (int i = 0; i < m.answers.size(); i++) {
        if (m.answers[i].rtype == DNS_TYPE_A) {
            out_ip = m.answers[i].a_ip;
            return true;
        }
    }
    return false;
}

bool dns_first_cname(const DnsMessage& m, String& out) {
    for (int i = 0; i < m.answers.size(); i++) {
        if (m.answers[i].rtype == DNS_TYPE_CNAME) {
            out = m.answers[i].cname;
            return true;
        }
    }
    return false;
}

bool dns_first_mx(const DnsMessage& m, String& out_exchange, uint16_t& out_pref) {
    for (int i = 0; i < m.answers.size(); i++) {
        if (m.answers[i].rtype == DNS_TYPE_MX) {
            out_exchange = m.answers[i].cname;
            out_pref = m.answers[i].mx_pref;
            return true;
        }
    }
    return false;
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [dns] FAIL: %s\n", what); }
}
} // namespace

int dns_self_test() {
    g_fails = 0;
    uint8_t buf[512];

    // 1) 构造查询
    int n = dns_build_query(buf, 0x1234, "www.example.com", DNS_TYPE_A);
    expect("dns query len", n == 12 + 17 + 4);     // 12 头 + 17 名字 + 4 尾
    expect("dns query id", be16(buf + 0) == 0x1234);
    expect("dns query rd", (buf[2] & 1) == 1);
    expect("dns query qd", be16(buf + 4) == 1);
    // 名字编码：偏移 12 处应为 0x03 'w' 'w' 'w'
    expect("dns name label1", buf[12] == 3 && buf[13] == 'w' && buf[15] == 'w');
    expect("dns name ends zero", buf[12 + 17 - 1] == 0);
    // AAAA 查询（独立缓冲，避免覆盖上面的 A 查询）
    uint8_t buf6[80];
    int n6 = dns_build_query(buf6, 0x0001, "ipv6.example.com", DNS_TYPE_AAAA);
    DnsMessage m6;
    expect("dns aaaa build", dns_parse(buf6, n6, m6) == 0 && m6.qdcount == 1);
    expect("dns class name", strcmp(dns_class_name(DNS_CLASS_IN), "IN") == 0);
    // qtype / qclass 在末尾
    expect("dns qtype", be16(buf + n - 4) == DNS_TYPE_A);
    expect("dns qclass", be16(buf + n - 2) == DNS_CLASS_IN);

    // 2) 解析回这个查询（问题段）
    DnsMessage m;
    int r = dns_parse(buf, n, m);
    expect("dns parse query", r == 0);
    expect("dns parse qdcount", m.qdcount == 1 && m.questions.size() == 1);
    expect("dns parse qname", m.questions[0].qname == "www.example.com");
    expect("dns parse qtype", m.questions[0].qtype == DNS_TYPE_A);

    // 3) 手工构造一个响应（含一条 A 记录，名字用指针压缩）
    //    头：id=0x1234 flags=0x8180 qd=1 an=1
    //    问题：3www7example3com0 + A + IN
    //    回答：指针 0xC00C + A + IN + TTL=300 + rdlen=4 + 1.2.3.4
    uint8_t resp[64];
    int o = 0;
    put_be16(resp + o, 0x1234); o += 2;
    put_be16(resp + o, 0x8180); o += 2;
    put_be16(resp + o, 1);      o += 2;   // QD
    put_be16(resp + o, 1);      o += 2;   // AN
    put_be16(resp + o, 0);      o += 2;
    put_be16(resp + o, 0);      o += 2;
    // question
    o += encode_name(resp + o, "www.example.com");
    put_be16(resp + o, DNS_TYPE_A); o += 2;
    put_be16(resp + o, DNS_CLASS_IN); o += 2;
    // answer (name pointer to offset 12)
    resp[o++] = 0xC0; resp[o++] = 0x0C;
    put_be16(resp + o, DNS_TYPE_A); o += 2;
    put_be16(resp + o, DNS_CLASS_IN); o += 2;
    put_be32(resp + o, 300);       o += 4;
    put_be16(resp + o, 4);         o += 2;
    resp[o++] = 1; resp[o++] = 2; resp[o++] = 3; resp[o++] = 4;

    r = dns_parse(resp, o, m);
    expect("dns resp parse", r == 0);
    expect("dns resp qr", m.qr);
    expect("dns resp ancount", m.ancount == 1 && m.answers.size() == 1);
    expect("dns resp owner", m.answers[0].name == "www.example.com");   // 指针解压
    expect("dns resp type", m.answers[0].rtype == DNS_TYPE_A);
    expect("dns resp ttl", m.answers[0].ttl == 300);
    expect("dns resp a ip", m.answers[0].a_ip == 0x01020304u);

    // 4) 类型名
    expect("dns type name", strcmp(dns_type_name(DNS_TYPE_CNAME), "CNAME") == 0);

    // 5) 辅助查询
    expect("dns count A", dns_count_by_type(m, DNS_TYPE_A) == 1);
    uint32_t ip;
    expect("dns first_a", dns_first_a(m, ip) && ip == 0x01020304u);
    expect("dns not truncated", !dns_is_truncated(m));

    expect("dns not nxdomain", !dns_is_nxdomain(m));

    return g_fails;
}

} // namespace netproto
} // namespace nefu
