// =============================================================================
//  netcmd.cpp —— 网络命令实现
// =============================================================================
#include "netcmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  IPv4 解析
// -----------------------------------------------------------------------------
bool ipv4_parse(const char* s, uint32_t* out) {
    if (!s || !out) return false;
    uint32_t parts[4];
    int cur = 0;
    const char* p = s;
    for (int oct = 0; oct < 4; oct++) {
        if (!is_digit(*p)) return false;
        int v = 0;
        while (is_digit(*p)) { v = v * 10 + (*p - '0'); p++; }
        if (v > 255) return false;
        parts[oct] = (uint32_t)v;
        if (oct < 3) {
            if (*p != '.') return false;
            p++;
        }
    }
    if (*p != 0) return false;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

void ipv4_format(uint32_t ip, char* out, int outsz) {
    uint8_t a = (uint8_t)(ip >> 24), b = (uint8_t)(ip >> 16),
            c = (uint8_t)(ip >> 8),  d = (uint8_t)(ip);
    term_snprintf(out, outsz, "%d.%d.%d.%d", a, b, c, d);
}

// -----------------------------------------------------------------------------
//  反码求和校验和
// -----------------------------------------------------------------------------
uint16_t internet_checksum(const uint8_t* data, int len) {
    uint32_t sum = 0;
    int i = 0;
    while (i + 1 < len) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1];
        i += 2;
    }
    if (i < len) sum += (uint32_t)data[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

// -----------------------------------------------------------------------------
//  路由表
// -----------------------------------------------------------------------------
static RouteEntry g_routes[16];
static int g_route_count = 0;

void route_table_reset() { g_route_count = 0; }

void route_table_add(uint32_t dest, uint32_t mask, uint32_t gw, const char* iface) {
    if (g_route_count >= 16) return;
    RouteEntry* e = &g_routes[g_route_count++];
    e->dest = dest & mask;
    e->mask = mask;
    e->gw = gw;
    nefu::strncpy(e->iface, iface, 15); e->iface[15] = 0;
}

// 最长前缀匹配
uint32_t route_lookup(uint32_t ip, char* iface_out, int iface_cap) {
    int best_bits = -1;
    uint32_t best_gw = 0;
    const char* best_iface = "";
    for (int i = 0; i < g_route_count; i++) {
        RouteEntry* e = &g_routes[i];
        if ((ip & e->mask) == e->dest) {
            int bits = 0;
            uint32_t m = e->mask;
            while (m & 0x80000000u) { bits++; m <<= 1; }
            if (bits > best_bits) {
                best_bits = bits; best_gw = e->gw; best_iface = e->iface;
            }
        }
    }
    if (best_bits < 0) return 0;
    if (iface_out) { nefu::strncpy(iface_out, best_iface, iface_cap - 1); iface_out[iface_cap - 1] = 0; }
    return best_gw;
}

// -----------------------------------------------------------------------------
//  ARP 表
// -----------------------------------------------------------------------------
static ArpEntry g_arp[16];
static int g_arp_count = 0;

void arp_table_reset() { g_arp_count = 0; }

void arp_add(uint32_t ip, const uint8_t mac[6]) {
    if (g_arp_count >= 16) return;
    g_arp[g_arp_count].ip = ip;
    for (int i = 0; i < 6; i++) g_arp[g_arp_count].mac[i] = mac[i];
    g_arp_count++;
}

bool arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    for (int i = 0; i < g_arp_count; i++) {
        if (g_arp[i].ip == ip) {
            for (int k = 0; k < 6; k++) mac_out[k] = g_arp[i].mac[k];
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
//  DNS
// -----------------------------------------------------------------------------
struct DnsRec { const char* name; uint32_t ip; };
bool dns_lookup(const char* name, uint32_t* out_ip) {
    if (!name || !out_ip) return false;
    static const DnsRec table[] = {
        {"localhost",  (127u << 24) | 1u},
        {"nefuos.dev", (10u << 24) | (0u << 16) | (0u << 8) | 2u},
        {"example.com",(93u << 24) | (184u << 16) | (216u << 8) | 34u},
        {"gateway",    (10u << 24) | (0u << 16) | (2u << 8) | 2u},
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (nefu::strcmp(table[i].name, name) == 0) { *out_ip = table[i].ip; return true; }
    }
    return false;
}

// -----------------------------------------------------------------------------
//  HTTP 响应解析
// -----------------------------------------------------------------------------
bool http_parse_response(const char* raw, int len, HttpResp* out) {
    if (!raw || !out || len < 12) return false;
    // 找 header/body 分界: "\r\n\r\n" 或 "\n\n"
    int body_off = -1;
    for (int i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i+1] == '\n' && raw[i+2] == '\r' && raw[i+3] == '\n') {
            body_off = i + 4; break;
        }
    }
    if (body_off < 0) {
        for (int i = 0; i + 1 < len; i++) {
            if (raw[i] == '\n' && raw[i+1] == '\n') { body_off = i + 2; break; }
        }
    }
    if (body_off < 0) return false;
    int header_len = body_off - 2; // 去掉结尾的空行
    // 第一行 = status line
    int eol = 0;
    while (eol < header_len && raw[eol] != '\r' && raw[eol] != '\n') eol++;
    out->status_line = nefu::String(raw, eol);
    out->headers = nefu::String(raw + eol, header_len - eol);
    out->body = nefu::String(raw + body_off, len - body_off);
    // 解析状态码: "HTTP/1.1 200 OK"
    // 跳过 "HTTP/1.x "
    int sp = 0;
    while (sp < eol && raw[sp] != ' ') sp++;
    while (sp < eol && raw[sp] == ' ') sp++;
    out->status = 0;
    while (sp < eol && is_digit(raw[sp])) {
        out->status = out->status * 10 + (raw[sp] - '0');
        sp++;
    }
    return out->status >= 100 && out->status <= 599;
}

// -----------------------------------------------------------------------------
//  命令
// -----------------------------------------------------------------------------
static int cmd_ping(int argc, const char** argv, TermOutput* out) {
    if (argc < 2) { out->pln("usage: ping <host>"); return 1; }
    uint32_t ip = 0;
    char ipstr[32];
    if (!ipv4_parse(argv[1], &ip)) {
        // 试 DNS
        if (!dns_lookup(argv[1], &ip)) { out->pfln("ping: unknown host %s", argv[1]); return 1; }
    }
    ipv4_format(ip, ipstr, sizeof(ipstr));
    out->pfln("PING %s (%s): 56 data bytes", argv[1], ipstr);
    // 构造一个真实 ICMP 回显请求头(8 字节)并算校验和, 打印校验值
    uint8_t icmp[8];
    for (int i = 0; i < 8; i++) icmp[i] = 0;
    icmp[0] = 8; // type=echo request
    icmp[1] = 0; // code
    icmp[4] = 1; icmp[5] = 0; // id
    icmp[6] = 0; icmp[7] = 1; // seq
    uint16_t cksum = internet_checksum(icmp, 8);
    Rng rng(1234);
    for (int i = 0; i < 4; i++) {
        int rtt = rng.range(1, 8);
        out->pfln("64 bytes from %s: icmp_seq=%d ttl=64 time=%d.%d ms",
                  ipstr, i, rtt, rng.next() % 10);
    }
    out->pfln("--- %s ping statistics ---", argv[1]);
    out->pfln("4 packets transmitted, 4 received, 0%% packet loss, icmp_cksum=0x%04X", cksum);
    return 0;
}

static int cmd_ifconfig(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    out->pln("eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500");
    out->pln("        inet 10.0.2.15  netmask 255.255.255.0  broadcast 10.0.2.255");
    out->pln("        inet6 fe80::1  prefixlen 64");
    out->pln("        ether 52:54:00:12:34:56  txqueuelen 1000");
    out->pln("        RX packets 1234  bytes 567890");
    out->pln("        TX packets 1100  bytes 432100");
    return 0;
}

static int cmd_route(int argc, const char** argv, TermOutput* out) {
    if (g_route_count == 0) {
        // 默认表
        route_table_add((10u << 24), (255u << 24), 0, "eth0");
        route_table_add(0, 0, (10u << 24) | 2u, "eth0"); // default
    }
    if (argc >= 2 && nefu::strcmp(argv[1], "get") == 0 && argc == 3) {
        uint32_t ip = 0;
        if (!ipv4_parse(argv[2], &ip)) { out->pln("route: bad address"); return 1; }
        char iface[16] = {0};
        uint32_t gw = route_lookup(ip, iface, sizeof(iface));
        char gws[32]; ipv4_format(gw, gws, sizeof(gws));
        out->pfln("via %s dev %s", gws, iface);
        return 0;
    }
    out->pln("Kernel IP routing table");
    out->pln("Destination     Gateway         Mask            Iface");
    for (int i = 0; i < g_route_count; i++) {
        char d[32], g[32], m[32];
        ipv4_format(g_routes[i].dest, d, sizeof(d));
        ipv4_format(g_routes[i].gw, g, sizeof(g));
        ipv4_format(g_routes[i].mask, m, sizeof(m));
        out->pfln("%-15s %-15s %-15s %s", d, g, m, g_routes[i].iface);
    }
    return 0;
}

static int cmd_arp(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    if (g_arp_count == 0) {
        uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
        arp_add((10u << 24) | 2u, mac);
    }
    out->pln("Address                  HWtype  HWaddress");
    for (int i = 0; i < g_arp_count; i++) {
        char ip[32]; ipv4_format(g_arp[i].ip, ip, sizeof(ip));
        out->pfln("%-24s ether   %02x:%02x:%02x:%02x:%02x:%02x",
                  ip, g_arp[i].mac[0], g_arp[i].mac[1], g_arp[i].mac[2],
                  g_arp[i].mac[3], g_arp[i].mac[4], g_arp[i].mac[5]);
    }
    return 0;
}

static int cmd_netstat(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    out->pln("Proto Recv-Q Send-Q Local Address    Foreign Address  State");
    out->pln("tcp        0      0 10.0.2.15:22     0.0.0.0:*        LISTEN");
    out->pln("tcp        0      0 10.0.2.15:80     0.0.0.0:*        LISTEN");
    out->pln("tcp        0      0 10.0.2.15:443    0.0.0.0:*        LISTEN");
    out->pln("udp        0      0 10.0.2.15:53     0.0.0.0:*");
    return 0;
}

static int cmd_nslookup(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: nslookup <name>"); return 1; }
    uint32_t ip = 0;
    if (!dns_lookup(argv[1], &ip)) { out->pfln("** server can't find %s: NXDOMAIN", argv[1]); return 1; }
    char ips[32]; ipv4_format(ip, ips, sizeof(ips));
    out->pln("Server:  nefuos.dns");
    out->pln("Address:  10.0.2.2");
    out->pln();
    out->pfln("Name:    %s", argv[1]);
    out->pfln("Address:  %s", ips);
    return 0;
}

// curl: 解析 URL, 然后解析一段内置"响应"并打印状态/头/正文
static int cmd_curl(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: curl <url>"); return 1; }
    // 解析 URL: http://host[:port]/path
    const char* url = argv[1];
    if (!str_starts(url, "http://")) { out->pln("curl: only http:// supported"); return 1; }
    const char* host = url + 7;
    const char* path = strchr(host, '/');
    char hostbuf[128];
    int hl = path ? (int)(path - host) : (int)nefu::strlen(host);
    if (hl > 127) hl = 127;
    for (int i = 0; i < hl; i++) hostbuf[i] = host[i];
    hostbuf[hl] = 0;
    out->pfln("* Connected to %s port 80", hostbuf);
    out->pfln("> GET %s HTTP/1.1", path ? path : "/");
    // 内置一段真实可解析的 HTTP 响应(测试用)
    const char* raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, nefu!";
    HttpResp resp;
    if (!http_parse_response(raw, (int)nefu::strlen(raw), &resp)) {
        out->pln("curl: bad response"); return 1;
    }
    out->pfln("< %s", resp.status_line.c_str());
    out->pfln("< Content-Type: text/plain");
    out->pln();
    out->pln(resp.body.c_str());
    return 0;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int netcmd_self_test() {
    int fails = 0;
    // 1) IPv4 解析/格式化
    {
        uint32_t ip = 0;
        if (!ipv4_parse("10.0.2.15", &ip)) fails++;
        char buf[32]; ipv4_format(ip, buf, sizeof(buf));
        if (nefu::strcmp(buf, "10.0.2.15") != 0) fails++;
        if (ipv4_parse("999.1.1.1", &ip)) fails++;
    }
    // 2) 校验和: 校验和字段先置 0, 算出后回填, 再算应为 0
    {
        uint8_t pkt[8] = {0x45, 0, 0, 0, 0x12, 0x34, 0, 0};
        uint16_t c = internet_checksum(pkt, 8);
        pkt[2] = (uint8_t)(c >> 8); pkt[3] = (uint8_t)(c & 0xff);
        uint16_t c2 = internet_checksum(pkt, 8);
        if (c2 != 0) fails++;
    }
    // 3) 路由最长前缀
    {
        route_table_reset();
        route_table_add((10u << 24), (255u << 24), (10u<<24)|1, "eth0");
        route_table_add((10u << 24)|(0u<<16)|(2u<<8), (255u<<24)|(255u<<16)|(255u<<8)|0,
                        (10u<<24)|2, "eth1");
        char iface[16] = {0};
        uint32_t gw = route_lookup((10u<<24)|(0u<<16)|(2u<<8)|5u, iface, sizeof(iface));
        if (gw != ((10u<<24)|2u)) fails++;
        if (nefu::strcmp(iface, "eth1") != 0) fails++;
    }
    // 4) ARP
    {
        arp_table_reset();
        uint8_t mac[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        arp_add((192u<<24)|(168u<<16)|1u, mac);
        uint8_t out[6];
        if (!arp_lookup((192u<<24)|(168u<<16)|1u, out)) fails++;
        if (out[0] != 0xaa || out[5] != 0xff) fails++;
        if (arp_lookup((192u<<24)|(168u<<16)|99u, out)) fails++;
    }
    // 5) DNS
    {
        uint32_t ip = 0;
        if (!dns_lookup("localhost", &ip)) fails++;
        if (((ip >> 24) & 0xff) != 127) fails++;
        if (dns_lookup("nohost.invalid", &ip)) fails++;
    }
    // 6) HTTP 解析
    {
        const char* raw =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<h1>missing</h1>";
        HttpResp r;
        if (!http_parse_response(raw, (int)nefu::strlen(raw), &r)) fails++;
        if (r.status != 404) fails++;
        if (r.body != "<h1>missing</h1>") fails++;
        if (r.headers.find("Content-Type") < 0) fails++;
    }
    // 7) ping 命令不崩且打印统计
    {
        const char* av[2] = {"ping", "localhost"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_ping(2, av, &o);
        if (!b.contains("ping statistics")) fails++;
    }
    // 8) nslookup
    {
        const char* av[2] = {"nslookup", "example.com"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_nslookup(2, av, &o);
        if (!b.contains("Address")) fails++;
    }
    // 9) curl
    {
        const char* av[2] = {"curl", "http://example.com/index"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_curl(2, av, &o);
        if (!b.contains("Hello, nefu!")) fails++;
    }
    return fails;
}

} // namespace termcmds
} // namespace nefu
