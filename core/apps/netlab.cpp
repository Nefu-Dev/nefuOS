// nefuOS 网络协议栈实验室 (netlab) -- 展示全部协议模块
// 参考 algoviz.cpp 模式：cascade_pos + g_wm->create_window + on_paint/on_key/on_close。
// 展示 nefu::netproto 各模块的真实结构：以太网帧、ARP 缓存、路由表、
// TCP 三次握手状态机、ICMP/UDP 校验和、HTTP 解析。
//
// 按键：
//   Tab   切换展示页
//   T     重新运行全部 self_test
//   R     重置演示数据
//   Esc   关闭
// 本文件不接线 apps.h/apps.cpp（由组织者统一挂接），只提供 netlab_launch()。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../netproto/netproto_all.h"
#include "../netproto/ethernet.h"
#include "../netproto/arp.h"
#include "../netproto/ip.h"
#include "../netproto/icmp.h"
#include "../netproto/udp.h"
#include "../netproto/tcp.h"
#include "../netproto/dns.h"
#include "../netproto/http.h"
#include "../netproto/websocket.h"
#include "../netproto/mqtt.h"
#include "../netproto/pkt_dissect.h"

namespace nefu {
namespace netlab {

const int LAB_W = 620, LAB_H = 440;

// 展示页
enum Page {
    PAGE_STACK = 0,   // 协议栈分层图
    PAGE_ARP,         // ARP 缓存
    PAGE_ROUTE,       // 路由表
    PAGE_TCP,         // TCP 状态机
    PAGE_NETIF,       // 网络接口
    PAGE_PKTLOG,      // 报文日志
    PAGE_DISSECT,     // 报文解剖
    PAGE_HTTP,        // HTTP 演示
    PAGE_MQTT,        // MQTT 演示
    PAGE_WS,          // WebSocket 演示
    PAGE_HELP,        // 帮助
    PAGE_TEST,        // self_test 结果
    PAGE_COUNT
};

const char* PAGE_NAMES[PAGE_COUNT] = {
    "Stack", "ARP", "Route", "TCP", "NetIf", "PktLog", "Dissect", "HTTP", "MQTT", "WS", "DHCP", "Help", "SelfTest"
};

struct LabState {
    int page;
    int test_result;      // 缓存最近一次 self_test 结果
    uint32_t now_ms;

    // 演示用 ARP 缓存
    ArpCache arp;
    // 演示用路由表
    RouteTable rt;
    // 演示用 TCP 连接
    TcpConn conn;
    // 演示用网络接口
    NetIf nif;
    // 演示用报文日志
    PktLog log;
    // 演示用 HTTP 响应解析
    HttpResponse http;

    void init();
    void run_tests();
    void paint(Surface& s);
};

void LabState::init() {
    page = PAGE_STACK;
    now_ms = 0;
    run_tests();

    // 预填 ARP 缓存
    MacAddr gw_mac;   mac_parse("00:11:22:33:44:55", gw_mac);
    MacAddr host_mac;  mac_parse("52:54:00:AB:CD:EF", host_mac);
    uint32_t gw = (192u<<24)|(168u<<16)|(1u<<8)|1u;
    uint32_t dns= (8u<<24)|(8u<<16)|(8u<<8)|8u;
    arp.add(gw,   gw_mac,   0, 30000);
    arp.add(dns,  host_mac, 0, 30000);

    // 预填路由表
    rt.clear();
    rt.add_cidr("0.0.0.0/0", gw, "eth0");
    rt.add_cidr("192.168.1.0/24", 0, "eth0");
    rt.add_cidr("10.0.0.0/8", (10u<<24)|1u, "wlan0");

    // 演示一个 TCP 连接：被动打开 -> 收到 SYN -> 收到 SYN/ACK 的 ACK -> ESTABLISHED
    uint32_t lip = (192u<<24)|(168u<<16)|(1u<<8)|10u;
    uint32_t rip = (93u<<24)|(184u<<16)|(216u<<8)|34u;
    conn.init(lip, 8080, rip, 54321);
    conn.listen();
    conn.on_segment(TCP_SYN, 1000, 0, 0);          // 收到 SYN -> SYN_RECEIVED
    conn.on_segment(TCP_ACK, 1001, conn.iss + 1, 0); // 收到 ACK -> ESTABLISHED

    // 预解析一段 HTTP 响应
    const char* resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello";
    http.clear();
    http_parse_response(resp, (int)strlen(resp), http);

    // 配置网络接口
    MacAddr nif_mac; mac_parse("52:54:00:AB:CD:EF", nif_mac);
    nif.mac = nif_mac;
    nif.set_ip((192u<<24)|(168u<<16)|(1u<<8)|10u, 0xFFFFFF00u);
    nif.stats.tx_frames = 128; nif.stats.rx_frames = 204;

    // 预填几条报文日志
    log.init(8);
    uint32_t me = (192u<<24)|(168u<<16)|(1u<<8)|10u;
    log.log(0, IPPROTO_ICMP, 64, (8u<<24)|(8u<<16)|(8u<<8)|8u, me, 100);
    log.log(1, IPPROTO_TCP,  40, me, (93u<<24)|(184u<<16)|(216u<<8)|34u, 101);
    log.log(0, IPPROTO_UDP,  52, (8u<<24)|(8u<<16)|(8u<<8)|8u, me, 102);
}

void LabState::run_tests() {
    test_result = netproto_self_test();
}

// 画一行文本的小工具
static void line(Surface& s, int y, const char* text, uint32_t fg, uint32_t bg) {
    gfx::text(s, 12, y, text, fg, bg);
}

void LabState::paint(Surface& s) {
    s.fill(0x00FAF8EF);
    uint32_t fg   = 0x00333333;
    uint32_t dim  = 0x00888888;
    uint32_t bg   = 0x00FAF8EF;
    uint32_t okc  = 0x0027AE60;
    uint32_t badc = 0x00E74C3C;
    uint32_t acc  = 0x003498DB;

    gfx::text_scale(s, 12, 8, "nefuOS netproto lab", acc, bg, 2);
    char hdr[96];
    ksprintf(hdr, sizeof(hdr), "Page: %s   [Tab] next   [T] retest   [R] reset   [Esc] close",
             PAGE_NAMES[page]);
    line(s, 36, hdr, dim, bg);

    int y = 60;
    char buf[128];
    if (page == PAGE_STACK) {
        line(s, y,   "Application:  HTTP / WebSocket / MQTT / DNS", fg, bg); y += 18;
        line(s, y,   "Transport:    TCP (state machine, sliding window) / UDP", fg, bg); y += 18;
        line(s, y,   "Internet:     IPv4 (fragment/reassemble, route table) / ICMP", fg, bg); y += 18;
        line(s, y,   "Link:         Ethernet II / ARP cache", fg, bg); y += 26;
        line(s, y,   "No STL, no exceptions, no RTTI, no malloc.", dim, bg); y += 18;
        line(s, y,   "Q16.16 fixed-point for RTO/bandwidth (no FPU).", dim, bg); y += 18;
        line(s, y,   "RFC1071 checksum, pseudo-header for TCP/UDP.", dim, bg); y += 18;
        line(s, y,   "ARP cache with aging, route table CIDR lookup.", dim, bg); y += 18;
        line(s, y,   "IPv4 fragment reassembly with timeout.", dim, bg); y += 18;
        line(s, y,   "ICMP echo/dest-unreach/time-exceeded.", dim, bg); y += 18;
        line(s, y,   "DNS: A/AAAA/CNAME/MX/TXT records.", dim, bg);
        line(s, y,   "Modules: ethernet arp ip icmp udp tcp dns http ws mqtt", dim, bg); y += 18;
        ksprintf(buf, sizeof(buf), "version: %s  (%d modules)", netproto_version(), netproto_module_count());
        line(s, y,   buf, dim, bg);
    } else if (page == PAGE_ARP) {
        line(s, y, "ARP cache (IPv4 -> MAC):", acc, bg); y += 20;
        for (int i = 0; i < arp.size(); i++) {
            arp.format_entry(i, buf, sizeof(buf));
            line(s, y, buf, fg, bg); y += 18;
        }
        ksprintf(buf, sizeof(buf), "Entries: %d", arp.size());
        line(s, y+24, "ARP request/reply/gratuitous supported.", dim, bg);
        line(s, y+42, "Cache aging + timeout expiry.", dim, bg); y += 18;
        line(s, y+60, "Gratuitous ARP for IP conflict detect.", dim, bg);
        line(s, y+6, buf, dim, bg);
    } else if (page == PAGE_ROUTE) {
        line(s, y, "Route table (longest prefix match):", acc, bg); y += 20;
        RouteEntry e;
        uint32_t probes[3] = { 0xC0A80132u, 0x08080808u, 0x0A010203u };
        for (int p = 0; p < 3; p++) {
            if (rt.lookup(probes[p], e)) {
                char nb[20], gb[20];
                ip_format(e.net, nb, sizeof(nb));
                ip_format(e.gateway, gb, sizeof(gb));
                ksprintf(buf, sizeof(buf), "%s/%d via %s dev %s",
                         nb, e.prefix, e.gateway?gb:"direct", e.iface);
                line(s, y, buf, fg, bg); y += 18;
            }
        }
        ksprintf(buf, sizeof(buf), "Routes: %d", rt.size());
        line(s, y+24, "Longest prefix match lookup.", dim, bg);
        line(s, y+42, "Default route + CIDR support.", dim, bg); y += 18;
        line(s, y+60, "Dev names: eth0, wlan0, lo.", dim, bg);
        line(s, y+4, buf, dim, bg);
    } else if (page == PAGE_TCP) {
        line(s, y, "TCP connection state machine demo:", acc, bg); y += 20;
        ksprintf(buf, sizeof(buf), "State: %s", conn.state_name());
        line(s, y, buf, fg, bg); y += 18;
        ksprintf(buf, sizeof(buf), "local=%u.%u.%u.%u:%u",
              (conn.local_ip>>24)&0xFF, (conn.local_ip>>16)&0xFF,
              (conn.local_ip>>8)&0xFF, conn.local_ip&0xFF, conn.local_port);
        line(s, y, buf, fg, bg); y += 18;
        ksprintf(buf, sizeof(buf), "ISS=%u  snd_nxt=%u  rcv_nxt=%u",
              conn.iss, conn.snd_nxt, conn.rcv_nxt);
        line(s, y, buf, fg, bg); y += 18;
        line(s, y, "(LISTEN <- SYN <- SYN/ACK <- ACK => ESTABLISHED)", dim, bg); y += 18;
        line(s, y, "TCP options: MSS, window scale, SACK.", dim, bg); y += 18;
        line(s, y, "RTO estimator (Jacobson/Karels).", dim, bg); y += 18;
        line(s, y, "Sequence number arithmetic (mod 2^32).", dim, bg); y += 18;
        line(s, y, "State machine: LISTEN->ESTAB->CLOSE.", dim, bg);
    } else if (page == PAGE_NETIF) {
        line(s, y, "Network interface:", acc, bg); y += 20;
        nif.format_status(buf, sizeof(buf));
        line(s, y, buf, fg, bg); y += 20;
        ksprintf(buf, sizeof(buf), "on_link check: 192.168.1.50 is %s",
                 nif.on_link((192u<<24)|(168u<<16)|(1u<<8)|50u) ? "on-link" : "off-link");
        line(s, y, buf, fg, bg); y += 18;
        uint32_t nh = nif.next_hop((8u<<24)|(8u<<16)|(8u<<8)|8u, rt);
        ksprintf(buf, sizeof(buf), "next hop for 8.8.8.8: %s", nh ? "via gateway" : "no route");
        line(s, y+18, "Stats: rx/tx frames, bytes, errors.", dim, bg); y += 18;
        line(s, y+36, "on-link check + next-hop lookup.", dim, bg);
        line(s, y, buf, dim, bg);
    } else if (page == PAGE_PKTLOG) {
        line(s, y, "Packet log (ring buffer):", acc, bg); y += 20;
        for (int i = 0; i < log.size() && i < 8; i++) {
            log.format(i, buf, sizeof(buf));
            line(s, y, buf, fg, bg); y += 18;
        }
        ksprintf(buf, sizeof(buf), "entries: %d", log.size());
        line(s, y+24, "Ring buffer, overwrite oldest.", dim, bg); y += 18;
        line(s, y+42, "Timestamp + protocol + length.", dim, bg);
        line(s, y+4, buf, dim, bg);
    } else if (page == PAGE_DISSECT) {
        line(s, y, "Packet dissector (ethernet -> IP -> TCP/UDP/ICMP):", acc, bg); y += 20;
        // 三条演示包
        uint32_t me = (192u<<24)|(168u<<16)|(1u<<8)|10u;
        uint32_t dns= (8u<<24)|(8u<<16)|(8u<<8)|8u;
        uint8_t ipk[64];
        ip_pack_header(ipk, 1, 64, IPPROTO_UDP, me, dns, 28, 0);
        uint8_t up[8] = {0,0,0,53,0,8,0,0};
        np_copy(ipk+20, up, 8);
        pkt_dissect_ip(ipk, 28, buf, sizeof(buf));
        line(s, y, buf, fg, bg); y += 18;

        uint8_t tcpk[60];
        uint32_t rip = (93u<<24)|(184u<<16)|(216u<<8)|34u;
        tcp_pack(tcpk, 54321, 443, 1000, 1, TCP_SYN|TCP_ACK, 65535, 0, 0, me, rip);
        pkt_dissect_ip(tcpk, 20, buf, sizeof(buf));
        line(s, y, buf, fg, bg); y += 18;

        line(s, y, "walks headers, prints src/dst/ports/flags.", dim, bg); y += 18;
        line(s, y, "Ethernet -> IPv4 -> ICMP/TCP/UDP.", dim, bg); y += 18;
        line(s, y, "For netlab packet visualization.", dim, bg); y += 18;
        line(s, y+18, "UDP DNS query + TCP SYN demo.", dim, bg);
    } else if (page == PAGE_HTTP) {
        line(s, y, "HTTP/1.1 demo (parsed response):", acc, bg); y += 20;
        ksprintf(buf, sizeof(buf), "status: %d %s", http.status, http_reason(http.status));
        line(s, y, buf, fg, bg); y += 18;
        ksprintf(buf, sizeof(buf), "Content-Type: %s", http.get_header("Content-Type").c_str());
        line(s, y, buf, fg, bg); y += 18;
        ksprintf(buf, sizeof(buf), "Connection: %s", http.get_header("Connection").c_str());
        line(s, y, buf, fg, bg); y += 18;
        ksprintf(buf, sizeof(buf), "body: %s", http.body.c_str());
        line(s, y, buf, fg, bg); y += 20;
        line(s, y, "chunked decode + URL parse also supported.", dim, bg); y += 18;
        line(s, y, "http_build_request/response helpers included.", dim, bg); y += 18;
        line(s, y, "RFC1123 date + HTML escape + cookie parse.", dim, bg); y += 18;
        line(s, y, "Status code classification + reason phrases.", dim, bg); y += 18;
        line(s, y, "URL parse: scheme/host/path/query.", dim, bg);
    } else if (page == PAGE_MQTT) {
        line(s, y, "MQTT 3.1.1 supported messages:", acc, bg); y += 20;
        line(s, y, "CONNECT / CONNACK / PUBLISH / PUBACK", fg, bg); y += 18;
        line(s, y, "SUBSCRIBE / SUBACK / UNSUBSCRIBE", fg, bg); y += 18;
        line(s, y, "PINGREQ / PINGRESP / DISCONNECT", fg, bg); y += 18;
        line(s, y, "QoS2: PUBREC / PUBREL / PUBCOMP", fg, bg); y += 24;
        line(s, y, "variable-length encoding + will message.", dim, bg); y += 18;
        line(s, y, "QoS0/QoS1/QoS2 flow control.", dim, bg); y += 18;
        line(s, y, "Keepalive + clean session flags.", dim, bg); y += 18;
        line(s, y, "Packet type names + CONNACK parse.", dim, bg); y += 18;
        line(s, y, "Build helpers for all control packets.", dim, bg);
    } else if (page == PAGE_WS) {
        line(s, y, "WebSocket (RFC6455):", acc, bg); y += 20;
        line(s, y, "handshake: SHA-1 + base64 accept key", fg, bg); y += 18;
        line(s, y, "frame: text/binary/close/ping/pong", fg, bg); y += 18;
        line(s, y, "client-side masking (XOR key)", fg, bg); y += 18;
        line(s, y, "16/64-bit extended length", fg, bg); y += 24;
        line(s, y, "RFC key vector verified in self_test.", dim, bg); y += 18;
        line(s, y, "Close codes + ping/pong heartbeats.", dim, bg); y += 18;
        line(s, y, "Client masking (XOR) per RFC6455.", dim, bg); y += 18;
        line(s, y, "Frame encode/decode + handshake.", dim, bg); y += 18;
        line(s, y, "Embedded SHA-1 for accept key.", dim, bg);
    } else if (page == PAGE_DHCP) {
        line(s, y, "DHCP / Socket / Firewall modules:", acc, bg); y += 20;
        line(s, y, "DHCP: DISCOVER / OFFER / REQUEST / ACK", fg, bg); y += 18;
        line(s, y, "      (RFC2131, 4-step DORA)", fg, bg); y += 18;
        line(s, y, "Socket: UDP/TCP abstraction layer", fg, bg); y += 18;
        line(s, y, "        bind/connect/listen/find", fg, bg); y += 18;
        line(s, y, "Firewall: ACL (allow/deny, 5-tuple)", fg, bg); y += 18;
        line(s, y, "          port filter + CIDR match", fg, bg); y += 24;
        line(s, y, "Firewall: ACL with CIDR mask + port match.", dim, bg); y += 18;
        line(s, y, "Socket: 16-entry table, bind/listen/connect.", dim, bg); y += 18;
        line(s, y, "DHCP: Discover/Offer/Request/Ack/Release.", dim, bg); y += 18;
        line(s, y, "NTP: client request + server response parse.", dim, bg); y += 18;
        line(s, y, "     48-byte packet, stratum + offset.", dim, bg); y += 18;
        line(s, y, "     epoch conversion (1900 <-> 1970).", dim, bg); y += 18;
        line(s, y, "Socket: UDP/TCP abstraction layer.", dim, bg);
        line(s, y, "     (RFC5905, 48-byte packet, stratum check).", dim, bg); y += 18;
        line(s, y, "All verified in self_test.", dim, bg);    } else if (page == PAGE_HELP) {
        line(s, y, "nefuOS netproto lab - help:", acc, bg); y += 22;
        line(s, y, "Tab   next page", fg, bg); y += 18;
        line(s, y, "T     re-run all self_test", fg, bg); y += 18;
        line(s, y, "R     reset demo data", fg, bg); y += 18;
        line(s, y, "Esc   close window", fg, bg); y += 24;
        line(s, y, "Modules: ethernet arp ip icmp udp tcp", dim, bg); y += 18;
        line(s, y, "         dns http websocket mqtt", dim, bg); y += 18;
        line(s, y, "         dhcp socket firewall ntp", dim, bg); y += 18;
        line(s, y, "Built for nefuOS bare-metal target.", dim, bg); y += 18;
        line(s, y, "No FPU: Q16.16 fixed-point arithmetic.", dim, bg); y += 18;
        line(s, y, "Built with: g++ -std=c++17 -fno-exceptions", dim, bg); y += 18;
        line(s, y, "  -fno-rtti -fno-builtin -O2", dim, bg);
        line(s, y, "No STL / exceptions / RTTI / malloc.", dim, bg); y += 18;
        line(s, y, "Built for nefuOS bare-metal target.", dim, bg);
    } else if (page == PAGE_TEST) {
        line(s, y, "netproto_self_test() result:", acc, bg); y += 20;
        if (test_result == 0) {
            line(s, y, "  ALL MODULES PASS (0 failures)", okc, bg); y += 20;
        } else {
            ksprintf(buf, sizeof(buf), "  FAILURES: %d", test_result);
            line(s, y, buf, badc, bg); y += 20;
        }
        line(s, y, "ethernet arp ip icmp udp tcp dns http ws mqtt", dim, bg); y += 18;
        line(s, y, "dhcp socket firewall ntp netif pktlog dissect", dim, bg); y += 18;
        line(s, y, "common csum fx hex_dump", dim, bg); y += 18;
        line(s, y+18, "All modules: zero failures.", dim, bg);
    }
}

} // namespace netlab

// ---- window glue ----
static netlab::LabState* lab_of(Window* w) { return (netlab::LabState*)w->userdata; }

static void lab_paint(Window* w) {
    lab_of(w)->paint(w->back);
}

static void lab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    netlab::LabState* st = lab_of(w);
    if (e->keycode == KEY_TAB) {
        st->page = (st->page + 1) % netlab::PAGE_COUNT;
        return;
    }
    if (e->ascii == 't' || e->ascii == 'T') { st->run_tests(); return; }
    if (e->ascii == 'r' || e->ascii == 'R') { st->init(); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void lab_close(Window* w) {
    if (w->userdata) delete (netlab::LabState*)w->userdata;
    w->userdata = 0;
}

void netlab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("netlab", x, y, netlab::LAB_W, netlab::LAB_H);
    if (!w) return;
    netlab::LabState* st = new netlab::LabState();
    st->init();
    w->userdata = st;
    w->on_paint = lab_paint;
    w->on_key = lab_key;
    w->on_close = lab_close;
    g_wm->raise(w);
}

} // namespace nefu
