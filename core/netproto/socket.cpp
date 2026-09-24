// socket.cpp - 轻量级 socket 抽象实现
#include "socket.h"
#include "netproto_common.h"
#include <string.h>

namespace nefu {
namespace netproto {

SocketTable::SocketTable() : used_(0) {
    np_zero(socks_, sizeof(socks_));
}

SocketTable::~SocketTable() {
}

int SocketTable::create(int type) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) return -1;
    for (int i = 0; i < NETPROTO_MAX_SOCKETS; i++) {
        if (socks_[i].state == SS_FREE) {
            np_zero(&socks_[i], sizeof(Socket));
            socks_[i].type = (uint8_t)type;
            socks_[i].state = SS_BOUND;   // 创建即视为已分配
            used_++;
            return i;
        }
    }
    return -2;
}

bool SocketTable::bind(int idx, uint32_t lip, uint16_t lport) {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return false;
    Socket* s = &socks_[idx];
    if (s->state == SS_FREE) return false;
    // 端口冲突检测
    for (int i = 0; i < NETPROTO_MAX_SOCKETS; i++) {
        if (i == idx) continue;
        if (socks_[i].state != SS_FREE &&
            socks_[i].local_port == lport &&
            socks_[i].local_ip == lip) return false;
    }
    s->local_ip = lip;
    s->local_port = lport;
    return true;
}

bool SocketTable::connect(int idx, uint32_t rip, uint16_t rport) {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return false;
    Socket* s = &socks_[idx];
    if (s->state == SS_FREE) return false;
    s->remote_ip = rip;
    s->remote_port = rport;
    s->state = SS_CONNECTED;
    return true;
}

bool SocketTable::listen(int idx, int backlog) {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return false;
    Socket* s = &socks_[idx];
    if (s->type != SOCK_STREAM) return false;
    if (s->local_port == 0) return false;
    s->state = SS_LISTENING;
    s->backlog = backlog;
    return true;
}

void SocketTable::close(int idx) {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return;
    if (socks_[idx].state != SS_FREE) {
        np_zero(&socks_[idx], sizeof(Socket));
        used_--;
    }
}

int SocketTable::find(uint32_t lip, uint16_t lport,
                      uint32_t rip, uint16_t rport) const {
    for (int i = 0; i < NETPROTO_MAX_SOCKETS; i++) {
        const Socket* s = &socks_[i];
        if (s->state == SS_FREE) continue;
        if (s->local_port == lport && s->local_ip == lip &&
            s->remote_port == rport && s->remote_ip == rip) return i;
    }
    return -1;
}

int SocketTable::find_listen(uint16_t lport) const {
    for (int i = 0; i < NETPROTO_MAX_SOCKETS; i++) {
        const Socket* s = &socks_[i];
        if (s->state == SS_LISTENING && s->local_port == lport) return i;
    }
    return -1;
}

int SocketTable::count() const { return used_; }

Socket* SocketTable::get(int idx) {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return 0;
    return &socks_[idx];
}

const Socket* SocketTable::get(int idx) const {
    if (idx < 0 || idx >= NETPROTO_MAX_SOCKETS) return 0;
    return &socks_[idx];
}

void SocketTable::format_line(int idx, char* buf, int buflen) const {
    if (!buf || buflen < 32) return;
    const Socket* s = get(idx);
    if (!s) { ksprintf(buf, buflen, "[%d] FREE", idx); return; }
    const char* tname = s->type == SOCK_DGRAM ? "UDP" :
                        s->type == SOCK_STREAM ? "TCP" : "?";
    const char* stname = "?";
    switch (s->state) {
        case SS_FREE: stname = "FREE"; break;
        case SS_BOUND: stname = "BOUND"; break;
        case SS_LISTENING: stname = "LISTEN"; break;
        case SS_CONNECTED: stname = "ESTAB"; break;
        case SS_CLOSING: stname = "CLOSE"; break;
    }
    uint8_t a0 = s->local_ip & 0xFF;
    uint8_t a1 = (s->local_ip >> 8) & 0xFF;
    uint8_t a2 = (s->local_ip >> 16) & 0xFF;
    uint8_t a3 = (s->local_ip >> 24) & 0xFF;
    ksprintf(buf, buflen, "[%d] %s %s %d.%d.%d.%d:%d rx=%d tx=%d",
             idx, tname, stname, a0, a1, a2, a3,
             s->local_port, s->rx_bytes, s->tx_bytes);
}

// 自检
static int g_fail = 0;
static void expect(const char* name, bool ok) {
    if (!ok) g_fail++;
}

int socket_self_test() {
    g_fail = 0;
    SocketTable tbl;

    // 1) 创建 UDP socket
    int u = tbl.create(SOCK_DGRAM);
    expect("sock create udp", u >= 0);
    expect("sock count", tbl.count() == 1);

    // 2) bind
    expect("sock bind", tbl.bind(u, 0, 12345));
    const Socket* su = tbl.get(u);
    expect("sock bound port", su && su->local_port == 12345);

    // 3) 端口冲突
    int u2 = tbl.create(SOCK_DGRAM);
    expect("sock bind conflict", !tbl.bind(u2, 0, 12345));

    // 4) UDP connect (语义)
    expect("sock udp connect", tbl.connect(u, 0x08080808u, 53));
    expect("sock udp remote", su->remote_port == 53);

    // 5) 查找
    expect("sock find", tbl.find(0, 12345, 0x08080808u, 53) == u);
    expect("sock find miss", tbl.find(0, 9999, 0x08080808u, 53) < 0);

    // 6) TCP listen
    int t = tbl.create(SOCK_STREAM);
    expect("sock create tcp", t >= 0);
    expect("sock tcp bind", tbl.bind(t, 0, 80));
    expect("sock tcp listen", tbl.listen(t, 5));
    expect("sock find listen", tbl.find_listen(80) == t);
    expect("sock find listen miss", tbl.find_listen(9090) < 0);

    // 7) TCP connect
    int c = tbl.create(SOCK_STREAM);
    expect("sock tcp bind2", tbl.bind(c, 0, 50000));
    expect("sock tcp connect", tbl.connect(c, 0x0A000001u, 80));

    // 8) close
    tbl.close(u2);
    expect("sock after close", tbl.get(u2)->state == SS_FREE);
    tbl.close(u);
    expect("sock count2", tbl.count() == 2);   // t + c

    // 9) 格式化
    char line[128];
    tbl.format_line(t, line, sizeof(line));
    expect("sock fmt", strstr(line, "TCP") != 0 &&
                      strstr(line, "LISTEN") != 0);

    // 10) 错误情况
    expect("sock bad create", tbl.create(99) < 0);
    expect("sock bad bind", !tbl.bind(-1, 0, 1));

    return g_fail;
}

} // namespace netproto
} // namespace nefu
