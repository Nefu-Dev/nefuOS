// standalone TLS 1.3 test: connect bloxd.io, GET /?g=classic, dump body head
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../core/net/tls13.h"

struct SockCtx { SOCKET s; };
static int recv_cb(void* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    SockCtx* sc = (SockCtx*)ctx;
    if (timeout_ms > 0) {
        fd_set f; FD_ZERO(&f); FD_SET(sc->s, &f);
        timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(0, &f, 0, 0, &tv) <= 0) return 0;
    }
    int r = recv(sc->s, (char*)buf, maxlen, 0);
    return r == SOCKET_ERROR ? -1 : r;
}
static int send_cb(void* ctx, const uint8_t* buf, int len) {
    return send(((SockCtx*)ctx)->s, (const char*)buf, len, 0);
}

int main() {
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    struct addrinfo hints, *res = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("bloxd.io", "443", &hints, &res) != 0) { printf("dns fail\n"); return 1; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) { printf("connect fail %d\n", WSAGetLastError()); return 1; }
    freeaddrinfo(res);
    printf("tcp ok\n");
    SockCtx sc; sc.s = s;
    char err[200];
    nefu::tls::Tls13Conn* c = nefu::tls::tls13_connect(recv_cb, send_cb, &sc, "bloxd.io", err, sizeof(err));
    if (!c) { printf("tls fail: %s\n", err); return 1; }
    printf("tls ok alpn=[%s]\n", nefu::tls::tls13_alpn(c));
    const char* req =
        "GET /?g=classic HTTP/1.1\r\n"
        "Host: bloxd.io\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n";
    int rl = (int)strlen(req);
    if (nefu::tls::tls13_send(c, (const uint8_t*)req, rl) != rl) { printf("send fail\n"); return 1; }
    printf("request sent %d bytes\n", rl);
    // read response: head then body, up to 64KB
    char head[8192]; int hl = 0;
    int total = 0;
    uint8_t buf[32768];
    for (int round = 0; round < 10; round++) {
        int r = nefu::tls::tls13_recv(c, buf, (int)sizeof(buf), 15000);
        if (r <= 0) { printf("recv round %d -> %d (EOF/timeout)\n", round, r); break; }
        total += r;
        if (hl < (int)sizeof(head) - 1) {
            int cp = r;
            if (cp > (int)sizeof(head) - 1 - hl) cp = (int)sizeof(head) - 1 - hl;
            memcpy(head + hl, buf, cp); hl += cp;
        }
        printf("recv round %d: %d bytes (total %d), first: %.*s\n", round, r, total, r < 40 ? r : 40, (char*)buf);
        if (total > 131072) break;
    }
    head[hl] = 0;
    printf("=== head (%d) ===\n%.500s\n", hl, head);
    printf("=== body sniff ===\n");
    // find body start
    char* body = strstr(head, "\r\n\r\n");
    if (body) {
        body += 4;
        printf("body head (%d bytes to head-end): %.300s\n", (int)(head + hl - body), body);
    }
    nefu::tls::tls13_close(c);
    closesocket(s);
    printf("done total=%d\n", total);
    return 0;
}
