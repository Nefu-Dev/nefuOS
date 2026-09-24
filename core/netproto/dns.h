// nefuOS 网络协议栈 —— DNS（RFC 1035）
//
// 报文头（12 字节）：
//   标识(2) 标志(2) 问题数(2) 答数(2) 权威数(2) 附加数(2)
//
// 名字用"标签序列"编码：每段一个长度字节 + 内容，以 0 结尾。
// 响应里为了省空间会用指针压缩：高两位为 11 的字节表示跳到某处。
//
// 本模块：构造 A/AAAA 查询，解析响应中的 A/CNAME/MX/TXT 记录。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

// 记录类型
const uint16_t DNS_TYPE_A     = 1;
const uint16_t DNS_TYPE_NS    = 2;
const uint16_t DNS_TYPE_CNAME = 5;
const uint16_t DNS_TYPE_MX    = 15;
const uint16_t DNS_TYPE_TXT   = 16;
const uint16_t DNS_TYPE_AAAA  = 28;
const uint16_t DNS_CLASS_IN   = 1;

struct DnsQuestion {
    String   qname;
    uint16_t qtype;
    uint16_t qclass;
};

struct DnsRecord {
    uint16_t rtype;
    uint32_t ttl;
    String   name;        // owner name（已解压）
    uint32_t a_ip;        // TYPE_A：主机序 IPv4
    String   cname;       // CNAME/NS/MX exchange/TXT 的文本内容
    uint16_t mx_pref;     // MX 优先级
};

struct DnsMessage {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount, ancount, nscount, arcount;
    bool     qr;          // true=响应
    uint8_t  rcode;
    List<DnsQuestion> questions;
    List<DnsRecord>   answers;

    void clear() {
        id = 0; flags = 0; qdcount = ancount = nscount = arcount = 0;
        qr = false; rcode = 0;
        questions.clear();
        answers.clear();
    }
    DnsMessage() { clear(); }
};

// 构造一个查询报文。out 足够大（建议 512 字节）。
// id：事务 ID；name：如 "www.example.com"；qtype：DNS_TYPE_A 等。
// 返回写入长度。
int dns_build_query(uint8_t* out, uint16_t id, const char* name, uint16_t qtype);

// 解析一个 DNS 报文（查询或响应）。成功返回 0，失败 -1。
int dns_parse(const uint8_t* pkt, int len, DnsMessage& out);

// 类型号 -> 名字
const char* dns_type_name(uint16_t t);
const char* dns_class_name(uint16_t c);

// ---- 查询辅助 ----
// 响应是否被截断（TC 位）。
bool dns_is_truncated(const DnsMessage& m);
// 是否 NXDOMAIN（rcode=3）。
bool dns_is_nxdomain(const DnsMessage& m);
// 回答中类型为 rtype 的记录条数。
int  dns_count_by_type(const DnsMessage& m, uint16_t rtype);
// 取第一条 A 记录的 IP（主机序）。找到返回 true。
bool dns_first_a(const DnsMessage& m, uint32_t& out_ip);
// 取第一条 CNAME/TXT/NS 的文本。
bool dns_first_cname(const DnsMessage& m, String& out);
// 取第一条 MX 记录的 exchange 与优先级。
bool dns_first_mx(const DnsMessage& m, String& out_exchange, uint16_t& out_pref);

int dns_self_test();

} // namespace netproto
} // namespace nefu
