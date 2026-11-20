/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS Win32 backend — WebSocket client (RFC 6455) on top of WinHTTP.
//
// This lives in its own translation unit on purpose: MinGW's <winhttp.h> and
// <wininet.h> declare the same typedefs (INTERNET_SCHEME, URL_COMPONENTS), so
// a file that includes both cannot compile. win32.cpp needs WinINet (plain
// HTTP fetches); this file needs only WinHTTP.
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"

namespace nefu {

// ===================== WebSocket client (RFC 6455) =====================
// WinHTTP provides a real TLS stack (certificates, SNI, ALPN, proxies) plus a
// native WebSocket upgrade. That matters here: the in-house TLS 1.3 client is
// fingerprint-blocked by large CDNs, and WinINet has no WebSocket support at
// all, so a game page could never open its live channel through either path.
//
// Threading model: one reader thread per socket blocks in
// WinHttpWebSocketReceive and appends whole messages to a lock-free SPSC
// ring. The frame loop (JS thread) only pops complete messages, so a stalled
// server can never freeze the UI and polling costs a few instructions.
namespace {

struct WsConn {
    bool     used;
    int      state;              // 0 connecting, 1 open, 2 closed
    volatile LONG closed;        // set by close()/reader to request shutdown
    volatile LONG done;          // reader thread has exited
    HINTERNET sess, conn, req, ws;
    uint8_t* ring;
    int      ring_cap;           // power of two
    volatile LONG head, tail;    // byte counters, NOT masked
    // message currently being consumed by the JS side
    uint8_t* cur;
    int      cur_len, cur_off, cur_bin;
    bool     cur_valid;
};

static WsConn s_wsc[8];
static const int WS_MAX = (int)(sizeof(s_wsc) / sizeof(s_wsc[0]));

static int ws_parse_url(const char* url, bool* secure,
                        char* host, int host_sz, int* port,
                        char* path, int path_sz) {
    const char* p = strstr(url, "://");
    if (!p) return 0;
    int sl = (int)(p - url);
    if (sl != 2 && sl != 3) return 0;                       // ws / wss
    if (sl == 2) { if (strncmp(url, "ws", 2) != 0) return 0; *secure = false; }
    else         { if (strncmp(url, "wss", 3) != 0) return 0; *secure = true; }
    p += 3;
    const char* slash = strchr(p, '/');
    const char* qmark = strchr(p, '?');
    const char* endhost = p + strlen(p);
    if (slash && slash < endhost) endhost = slash;
    if (qmark && qmark < endhost) endhost = qmark;
    const char* colon = 0;
    for (const char* q = p; q < endhost; q++) if (*q == ':') { colon = q; break; }
    *port = *secure ? 443 : 80;
    const char* he = colon ? colon : endhost;
    if (colon) {
        char ps[8]; int pl = (int)(endhost - colon - 1);
        if (pl <= 0 || pl > 5) return 0;
        memcpy(ps, colon + 1, pl); ps[pl] = 0;
        *port = atoi(ps);
    }
    int hl = (int)(he - p);
    if (hl <= 0 || hl >= host_sz) return 0;
    memcpy(host, p, hl); host[hl] = 0;
    if (slash) {
        int pl = (int)strlen(slash);
        if (pl >= path_sz) pl = path_sz - 1;
        memcpy(path, slash, pl); path[pl] = 0;
    } else if (qmark) {
        int pl = (int)strlen(qmark);
        if (pl >= path_sz - 1) pl = path_sz - 2;
        path[0] = '/';
        memcpy(path + 1, qmark, pl); path[pl + 1] = 0;
    } else {
        path[0] = '/'; path[1] = 0;
    }
    return 1;
}

static void ws_ring_in(WsConn* c, int pos, const uint8_t* src, int n) {
    int cap = c->ring_cap, first = cap - pos;
    if (first > n) first = n;
    memcpy(c->ring + pos, src, (size_t)first);
    if (n > first) memcpy(c->ring, src + first, (size_t)(n - first));
}

static void ws_ring_out(WsConn* c, int pos, uint8_t* dst, int n) {
    int cap = c->ring_cap, first = cap - pos;
    if (first > n) first = n;
    memcpy(dst, c->ring + pos, (size_t)first);
    if (n > first) memcpy(dst + first, c->ring, (size_t)(n - first));
}

// Append one whole message: [1 byte type][4 byte len][payload]. Drops the
// message (rather than blocking or growing without bound) when the consumer
// falls too far behind.
static bool ws_push(WsConn* c, int type, const uint8_t* p, int len) {
    if (!c || !c->ring || len < 0) return false;
    int cap = c->ring_cap;
    int need = 5 + len;
    if (need > cap - 1) return false;
    LONG head = c->head, tail = c->tail;
    int used = (int)(head - tail);
    if (cap - 1 - used < need) return false;
    uint8_t hdr[5];
    hdr[0] = (uint8_t)type;
    hdr[1] = (uint8_t)(len & 0xFF);
    hdr[2] = (uint8_t)((len >> 8) & 0xFF);
    hdr[3] = (uint8_t)((len >> 16) & 0xFF);
    hdr[4] = (uint8_t)((len >> 24) & 0xFF);
    ws_ring_in(c, (int)(head & (cap - 1)), hdr, 5);
    if (len) ws_ring_in(c, (int)((head + 5) & (cap - 1)), p, len);
    c->head = head + need;      // publish only after the bytes are in place
    return true;
}

// Move the next message into the "current" slot. 1 = ready, 0 = nothing yet,
// -1 = socket closed and drained.
static int ws_pop(WsConn* c) {
    if (c->cur_valid) return 1;
    int cap = c->ring_cap;
    if (!cap || !c->ring) return c->state == 2 ? -1 : 0;
    LONG head = c->head, tail = c->tail;
    int used = (int)(head - tail);
    if (used == 0) {
        // Rewind the counters only once the producer has exited, so a very
        // long session cannot wrap the signed 32-bit distance. (Touching them
        // while the reader thread is live would race with a concurrent push.)
        if (c->done) { c->head = 0; c->tail = 0; }
        return c->state == 2 ? -1 : 0;
    }
    if (used < 5) return c->state == 2 ? -1 : 0;
    uint8_t hdr[5];
    ws_ring_out(c, (int)(tail & (cap - 1)), hdr, 5);
    int len = (int)(hdr[1] | (hdr[2] << 8) | (hdr[3] << 16) | ((uint32_t)hdr[4] << 24));
    if (len < 0 || used < 5 + len) return c->state == 2 ? -1 : 0;
    uint8_t* buf = len > 0 ? (uint8_t*)kalloc((size_t)len) : 0;
    if (len > 0 && !buf) return 0;                  // out of memory: retry later
    if (len) ws_ring_out(c, (int)((tail + 5) & (cap - 1)), buf, len);
    c->tail = tail + 5 + len;
    c->cur = buf; c->cur_len = len; c->cur_off = 0;
    c->cur_bin = (hdr[0] == 2);
    c->cur_valid = true;
    return 1;
}

static void ws_reader(void* arg) {
    WsConn* c = (WsConn*)arg;
    uint8_t buf[8192];
    uint8_t* acc = 0;
    int acc_len = 0, acc_cap = 0, acc_bin = 0;
    for (;;) {
        if (c->closed) break;
        if (!c->ws) break;
        DWORD got = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bt = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        DWORD rc = WinHttpWebSocketReceive(c->ws, buf, (DWORD)sizeof(buf), &got, &bt);
        if (rc != ERROR_SUCCESS) {
            // A 1006 in the page means the reader died here; without the code
            // there is no way to tell a peer close from a local receive error.
            char eb[160];
            wsprintfA(eb, "ws: recv err=%lu got=%lu bt=%lu", (unsigned long)rc,
                      (unsigned long)got, (unsigned long)bt);
            platform_dbg(eb);
            break;
        }
        if (bt == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            // The peer close frame carries the reason (status code + text); a
            // bare "1006" in the page tells us nothing about why the game
            // server dropped us, so dump it before echoing.
            char cb[220];
            int n = 0;
            n += wsprintfA(cb + n, "ws: peer close frame got=%lu hex=", (unsigned long)got);
            for (DWORD i = 0; i < got && i < 24 && n < 200; i++)
                n += wsprintfA(cb + n, "%02x", (unsigned)buf[i]);
            platform_dbg(cb);
            // echo the close so the peer sees a clean shutdown
            WinHttpWebSocketClose(c->ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
            break;
        }
        bool fin = (bt == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                    bt == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
        bool bin = (bt == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                    bt == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE);
        if (got == 0 && !fin) continue;
        if (acc_len == 0 && fin) {
            ws_push(c, bin ? 2 : 1, buf, (int)got);
        } else {
            if (acc_len + (int)got > acc_cap) {
                int want = acc_len + (int)got;
                int ncap = acc_cap ? acc_cap : 4096;
                while (ncap < want) ncap *= 2;
                uint8_t* np = acc ? (uint8_t*)krealloc(acc, (size_t)ncap)
                                  : (uint8_t*)kalloc((size_t)ncap);
                if (!np) { acc_len = 0; continue; }
                acc = np; acc_cap = ncap;
            }
            if (acc_len == 0) acc_bin = bin ? 1 : 0;
            memcpy(acc + acc_len, buf, (size_t)got);
            acc_len += (int)got;
            if (fin) {
                ws_push(c, acc_bin ? 2 : 1, acc, acc_len);
                acc_len = 0;
            }
        }
    }
    if (acc) kfree(acc);
    c->state = 2;
    c->closed = 1;
    c->done = 1;
}

static WsConn* ws_get(int h) {
    if (h < 0 || h >= WS_MAX) return 0;
    WsConn* c = &s_wsc[h];
    return c->used ? c : 0;
}

} // namespace

int platform_ws_open(const char* url) { return platform_ws_open_hdr(url, 0); }

int platform_ws_open_hdr(const char* url, const char* extra_headers) {
    if (!url || !url[0]) return -1;
    bool secure = false;
    char host[128], path[1024];
    int port = 0;
    if (!ws_parse_url(url, &secure, host, sizeof(host), &port, path, sizeof(path))) {
        platform_dbg("ws: bad url");
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < WS_MAX; i++) {
        if (!s_wsc[i].used) { slot = i; break; }
        if (s_wsc[i].done) {                       // previous owner has exited
            if (s_wsc[i].cur) { kfree(s_wsc[i].cur); s_wsc[i].cur = 0; }
            memset(&s_wsc[i], 0, sizeof(WsConn));
            slot = i; break;
        }
    }
    if (slot < 0) { platform_dbg("ws: no free slot"); return -1; }
    WsConn* c = &s_wsc[slot];
    memset(c, 0, sizeof(WsConn));
    c->used = true;
    c->state = 0;

    WCHAR whost[128], wpath[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 128) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) <= 0) {
        c->used = false;
        return -1;
    }
    c->sess = WinHttpOpen(L"nefuOS/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->sess) goto fail;
    // 3s connect / 5s send; receive is infinite because a dedicated thread
    // blocks in it.
    WinHttpSetTimeouts(c->sess, 3000, 3000, 5000, 0);
    {
        // WinHTTP keeps its own cookie store and would re-attach Set-Cookie values
        // on top of the header the JS jar already supplied. Two Cookie headers are
        // illegal on the wire and servers that read the first one then see no
        // identity at all - the room socket is dropped right after the upgrade.
        DWORD off = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(c->sess, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
    }
    c->conn = WinHttpConnect(c->sess, whost, (INTERNET_PORT)port, 0);
    if (!c->conn) goto fail;
    c->req = WinHttpOpenRequest(c->conn, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                secure ? WINHTTP_FLAG_SECURE : 0);
    if (!c->req) goto fail;
    if (!WinHttpSetOption(c->req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) goto fail;
    {
        // WinHTTP sends no cookie on the Upgrade request, so a Colyseus room
        // handshake that the server authenticates by cookie returns 401 and the
        // client never gets past JOIN_ROOM. Extra headers come from the JS
        // cookie jar (same text form the HTTP path already uses).
        //
        // The fixed 1024-WCHAR buffer used to be a silent trap: bloxd's jar
        // carries a signed identity cookie that is itself ~1.4 KB, so the
        // combined header block overflowed, MultiByteToWideChar failed, and the
        // code then sent *no* additional headers at all - no Origin, no Cookie,
        // no UA. The room upgrade still returned 101 (nginx does not check
        // Origin) and the game server kicked the anonymous socket right after,
        // which looked like a server-side auth mystery. Size the buffer from
        // the string and keep an explicit cap so a runaway jar cannot allocate
        // megabytes.
        WCHAR* wextra = 0;
        DWORD  wlen = 0;
        if (extra_headers && extra_headers[0]) {
            int need = MultiByteToWideChar(CP_UTF8, 0, extra_headers, -1, NULL, 0);
            if (need > 0 && need <= 16384) {
                wextra = (WCHAR*)kalloc((size_t)need * sizeof(WCHAR));
                if (wextra) {
                    int got = MultiByteToWideChar(CP_UTF8, 0, extra_headers, -1, wextra, need);
                    if (got > 0) wlen = (DWORD)(got - 1);
                }
            }
            // Byte count only: the header block holds credentials, so the log
            // stays at the size and whether the copy succeeded.
            char hb[120];
            wsprintfA(hb, "ws: hdr bytes=%d wchars=%lu%s", (int)strlen(extra_headers),
                      (unsigned long)wlen, (need > 16384) ? " (over cap)" : "");
            platform_dbg(hb);
        }
        BOOL sent = WinHttpSendRequest(c->req,
                                       (wextra && wlen) ? wextra : WINHTTP_NO_ADDITIONAL_HEADERS,
                                       (wextra && wlen) ? wlen : 0, NULL, 0, 0, 0);
        if (wextra) { kfree(wextra); wextra = 0; }
        if (!sent) goto fail;
    }
    if (!WinHttpReceiveResponse(c->req, NULL)) goto fail;
    {
        DWORD status = 0, sz = sizeof(status);
        if (!WinHttpQueryHeaders(c->req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                 WINHTTP_NO_HEADER_INDEX)) goto fail;
        if (status != 101) {
            char eb[160]; wsprintfA(eb, "ws: status %u host=%s", (unsigned)status, host);
            platform_dbg(eb);
            goto fail;
        }
        // Log the upgrade response headers: a server that accepts the upgrade and
        // then closes immediately usually says why here (subprotocol mismatch,
        // proxy hint, alternate room id).
        {
            // RAW_HEADERS_CRLF comes back as UTF-16LE (WinHttpQueryHeaders always
            // answers wide), so a char-sized copy only ever saw the first letter.
            WCHAR rb[4096];
            DWORD rsz = (DWORD)sizeof(rb) - sizeof(WCHAR);
            if (WinHttpQueryHeaders(c->req, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, rb, &rsz,
                                    WINHTTP_NO_HEADER_INDEX)) {
                char u8[4096];
                int un = WideCharToMultiByte(CP_UTF8, 0, rb, -1, u8, (int)sizeof(u8) - 1, NULL, NULL);
                if (un <= 0) un = 0;
                char eb[1024];
                int k = 0;
                for (int i = 0; i < un && i < 1000 && u8[i] != 0; i++) {
                    if (u8[i] == '\r' || u8[i] == '\n') { if (k && eb[k - 1] != '|') eb[k++] = '|'; }
                    else eb[k++] = u8[i];
                }
                eb[k] = 0;
                char lb[1100]; wsprintfA(lb, "ws: 101 hdr %s", eb);
                platform_dbg(lb);
            } else {
                char lb[80]; wsprintfA(lb, "ws: 101 hdrdump err=%lu", (unsigned long)GetLastError());
                platform_dbg(lb);
            }
        }
    }
    c->ws = WinHttpWebSocketCompleteUpgrade(c->req, 0);
    WinHttpCloseHandle(c->req);
    c->req = 0;
    if (!c->ws) goto fail;

    c->ring_cap = 128 * 1024;                       // power of two (masked index)
    c->ring = (uint8_t*)kalloc((size_t)c->ring_cap);
    if (!c->ring) goto fail;
    c->head = c->tail = 0;
    if (!platform_thread_create_stk(ws_reader, c, 192 * 1024)) goto fail;
    c->state = 1;
    {
        char eb[200]; wsprintfA(eb, "ws: open %s://%s:%d%s", secure ? "wss" : "ws",
                                host, port, path);
        platform_dbg(eb);
    }
    return slot;

fail:
    if (c->ws) { WinHttpCloseHandle(c->ws); c->ws = 0; }
    if (c->req) { WinHttpCloseHandle(c->req); c->req = 0; }
    if (c->conn) { WinHttpCloseHandle(c->conn); c->conn = 0; }
    if (c->sess) { WinHttpCloseHandle(c->sess); c->sess = 0; }
    c->state = 2;
    c->used = false;
    platform_dbg("ws: open failed");
    return -1;
}

int platform_ws_state(int h) {
    WsConn* c = ws_get(h);
    return c ? c->state : 2;
}

int platform_ws_send(int h, const uint8_t* data, int len, int binary) {
    WsConn* c = ws_get(h);
    if (!c || !c->ws || c->state != 1 || !data || len <= 0) return -1;
    DWORD rc = WinHttpWebSocketSend(c->ws,
        binary ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE
               : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)data, (DWORD)len);
    return rc == ERROR_SUCCESS ? len : -1;
}

int platform_ws_peek(int h, int* out_binary) {
    WsConn* c = ws_get(h);
    if (!c) return -1;
    int r = ws_pop(c);
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (out_binary) *out_binary = c->cur_bin;
    return c->cur_len;
}

int platform_ws_read(int h, uint8_t* buf, int cap) {
    WsConn* c = ws_get(h);
    if (!c || !c->cur_valid || !buf || cap <= 0) return 0;
    int n = c->cur_len - c->cur_off;
    if (n > cap) n = cap;
    if (n > 0) memcpy(buf, c->cur + c->cur_off, (size_t)n);
    c->cur_off += n;
    if (c->cur_off >= c->cur_len) {
        if (c->cur) { kfree(c->cur); c->cur = 0; }
        c->cur_len = c->cur_off = 0;
        c->cur_valid = false;
    }
    return n;
}

void platform_ws_close(int h) {
    WsConn* c = ws_get(h);
    if (!c) return;
    c->closed = 1;
    if (c->ws) {
        WinHttpWebSocketClose(c->ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        WinHttpCloseHandle(c->ws);
        c->ws = 0;
    }
    if (c->req) { WinHttpCloseHandle(c->req); c->req = 0; }
    if (c->conn) { WinHttpCloseHandle(c->conn); c->conn = 0; }
    if (c->sess) { WinHttpCloseHandle(c->sess); c->sess = 0; }
    if (c->cur) { kfree(c->cur); c->cur = 0; }
    c->cur_len = c->cur_off = 0;
    c->cur_valid = false;
    c->state = 2;
}


} // namespace nefu

