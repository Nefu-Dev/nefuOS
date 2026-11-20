/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// core/net/httpbare.cpp
//
// Bare-metal HTTP(S) client (see httpbare.h).
//
// Pipeline: parse URL -> static-DNS -> tcp_connect -> (TLS 1.3 if https) ->
// build request -> send -> read response headers -> read body (Content-Length
// or read-until-close, with chunked decoding for servers that use it).
//
// Freestanding: no STL, no exceptions. All buffers via kalloc/kfree.

#include "httpbare.h"
#include "net.h"          // nefu::tcp_connect / tcp_send / tcp_recv / tcp_close
#include "../klib/klib.h"
#include "tls13.h"
#include "../platform.h"
#include "gunzip.h"
#include <string.h>

namespace nefu {
namespace net {

// ---------------------------------------------------------------------------
// static DNS table (the bare NIC path has no live resolver wired yet)
// ---------------------------------------------------------------------------
static uint32_t hb_dns_lookup(const char* name) {
    // Static table + real DNS fallback now live in nefu::net::dns_lookup.
    return dns_lookup(name);
}

// ---------------------------------------------------------------------------
// growing buffer
// ---------------------------------------------------------------------------
struct HbBuf {
    uint8_t* p;
    uint32_t cap;
    uint32_t len;
    HbBuf() : p(0), cap(0), len(0) {}
    bool append(const uint8_t* d, uint32_t n) {
        if (len + n >= cap) {
            // Keep the capacity STRICTLY greater than the length: browser.cpp
            // NUL-terminates the returned body in place (js[blen] = 0) before
            // handing it to the JS engine. With a power-of-two capacity that
            // write lands one byte past the allocation whenever len == cap,
            // corrupting the next heap block header. Grow one step further so
            // there is always a spare byte for the terminator.
            uint32_t ncap = cap ? cap : 1024;
            while (ncap <= len + n) ncap *= 2;
            uint8_t* np = (uint8_t*)kalloc(ncap);
            if (!np) return false;
            if (p) { memcpy(np, p, len); kfree(p); }
            p = np; cap = ncap;
        }
        memcpy(p + len, d, n);
        len += n;
        return true;
    }
    void free() { if (p) { kfree(p); p = 0; cap = len = 0; } }
};

// Idle timeout for reading a response.
//
// The limit applies to *stalls*, not to the total transfer time: a 3 MB
// JavaScript bundle on a slow link legitimately takes minutes, while a peer
// that stops sending is detected after a short silence. The deadline is
// therefore pushed forward every time bytes actually arrive.
//
// 45 s (was 15 s): with delayed/batched ACKs the sender can fall back to RTO
// retransmission and go quiet for well over 15 s while still recovering.  A
// short silence limit aborted otherwise-good chunked responses just before
// their terminating 0-length chunk (seen under WHPX: 199 KB decoded, then a
// 15 s stall).  The deadline still resets on every byte, so a truly dead peer
// is detected in 45 s, not 45 s per transfer.
#define HB_IDLE_TIMEOUT_MS 45000
static int g_tls_fd = -1;
static int hb_tls_recv(void* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    (void)ctx;
    return tcp_recv(g_tls_fd, buf, maxlen, timeout_ms);
}
static int hb_tls_send(void* ctx, const uint8_t* buf, int len) {
    (void)ctx;
    return tcp_send(g_tls_fd, buf, len);
}

// Receive outcome codes shared by the TLS and the plain-TCP path.
#define HB_R_TIMEOUT (-1)   // no data yet (socket timeout) - retry
#define HB_R_EOF     0      // peer closed the connection
// Anything > 0 is the number of bytes received.

// Read from the connection, normalising the TLS and plain-TCP semantics into
// one contract: >0 = bytes, 0 = EOF, HB_R_TIMEOUT = nothing available yet.
// The bare TCP layer already reports "timeout" as -1 and "closed" as 0; the
// TLS layer uses the same convention (see tls13.h). Treating a timeout as an
// end-of-response truncates the transfer, so the distinction is preserved.
static int hb_recv(nefu::tls::Tls13Conn* tls, int fd, uint8_t* buf, int len, int timeout_ms) {
    if (tls) {
        int r = nefu::tls::tls13_recv(tls, buf, len, timeout_ms);
        if (r == -1) return HB_R_TIMEOUT;   // no data yet
        if (r == -2) return -2;              // fatal
        return r;                            // >0 data, 0 EOF
    }
    int r = tcp_recv(fd, buf, len, timeout_ms);
    if (r == -1) return HB_R_TIMEOUT;        // bare tcp_recv: -1 = timeout
    return r;                                 // >0 data, 0 closed
}

// Heartbeat for the read loops. Driven by an iteration counter rather than
// the tick so it still reports when platform_tick_ms() has stopped advancing
// - that is exactly the failure mode that makes the idle deadline unreachable.
// tick= is included so a frozen clock is visible directly in the log.
static void hb_beat(const char* phase, uint32_t iter, uint32_t have, int r) {
    if (iter % 20u == 0u)
        klogf("hb: wait %s it=%u tick=%u have=%u r=%d\n",
              phase, iter, (unsigned)platform_tick_ms(), (unsigned)have, r);
}

// ---------------------------------------------------------------------------
// response header helpers
// ---------------------------------------------------------------------------
// find "Name:" (case-insensitive) in the header block [0,hlen); *vlen = value
// length (trailing \r trimmed). Returns pointer to the value or 0.
static const char* hb_find_hdr(const char* h, int hlen, const char* name, int* vlen) {
    int nl = (int)strlen(name);
    int pos = 0;
    while (pos < hlen) {
        int eol = pos;
        while (eol < hlen && h[eol] != '\r' && h[eol] != '\n') eol++;
        if (eol > pos + nl && h[pos + nl] == ':') {
            bool match = true;
            for (int i = 0; i < nl; i++) {
                char a = h[pos + i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { match = false; break; }
            }
            if (match) {
                int v = pos + nl + 1;
                while (v < eol && (h[v] == ' ' || h[v] == '\t')) v++;
                *vlen = eol - v;
                return h + v;
            }
        }
        pos = eol;
        while (pos < hlen && (h[pos] == '\r' || h[pos] == '\n')) pos++;
    }
    return 0;
}

static int hb_find_cl(const char* h, int hlen) {
    int vlen = 0;
    const char* v = hb_find_hdr(h, hlen, "Content-Length", &vlen);
    if (!v || vlen <= 0) return -1;
    int n = 0;
    for (int i = 0; i < vlen && v[i] >= '0' && v[i] <= '9'; i++) n = n * 10 + (v[i] - '0');
    return n;
}

// does the transfer use chunked encoding?
static bool hb_is_chunked(const char* h, int hlen) {
    int vlen = 0;
    const char* v = hb_find_hdr(h, hlen, "Transfer-Encoding", &vlen);
    if (!v) return false;
    for (int i = 0; i + 7 <= vlen; i++) {
        if (strncmp(v + i, "chunked", 7) == 0) return true;
    }
    return false;
}

// ---- Set-Cookie capture -------------------------------------------------
// Real sites keep their session in cookies (login token, CSRF nonce, consent
// flags). The bare stack has no shared cookie store, so the cookies of the
// most recent response are collected here and handed to the JS document jar by
// the caller as soon as the request returns.
static char s_set_cookie[1024];
// Response headers of the last completed exchange, flattened to
// "Name: value\n" lines (see hb_capture_headers).
static char s_last_hdrs[2048];

static bool hb_ci_pref(const uint8_t* s, int n, const char* pref) {
    int pl = (int)strlen(pref);
    if (n < pl) return false;
    for (int i = 0; i < pl; i++) {
        uint8_t a = s[i];
        if (a >= 'A' && a <= 'Z') a = (uint8_t)(a - 'A' + 'a');
        if (a != (uint8_t)pref[i]) return false;
    }
    return true;
}

// Collect every "Set-Cookie:" line verbatim (attributes included) and join
// them with '\n'. Each line is exactly what document.cookie accepts, so the JS
// jar can replay them through its normal setter - which is where attribute
// handling and "Max-Age=0 means delete" already live.
static void hb_capture_set_cookie(const uint8_t* h, int hlen) {
    s_set_cookie[0] = 0;
    int p = 0;
    while (p < hlen && h[p] != '\n') p++;          // skip the status line
    int used = 0;
    while (p < hlen) {
        int ls = p + 1;
        if (ls >= hlen) break;
        int le = ls;
        while (le < hlen && h[le] != '\r' && h[le] != '\n') le++;
        if (le - ls > 11 && hb_ci_pref(h + ls, le - ls, "set-cookie:")) {
            int vs = ls + 11;
            while (vs < le && h[vs] == ' ') vs++;
            int ve = le;
            while (ve > vs && (h[ve - 1] == ' ' || h[ve - 1] == '\t')) ve--;
            if (ve > vs && used + (ve - vs) + 2 < (int)sizeof(s_set_cookie)) {
                if (used) s_set_cookie[used++] = '\n';
                memcpy(s_set_cookie + used, h + vs, (size_t)(ve - vs));
                used += ve - vs;
                s_set_cookie[used] = 0;
            }
        }
        p = le;
        if (p < hlen && h[p] == '\r') p++;
        if (p < hlen && h[p] == '\n') p++;
    }
}

// Cookies set by the last completed request, one raw "name=value; attrs" line
// per '\n'. Empty when the response carried none.
const char* httpbare_last_set_cookie() { return s_set_cookie; }

// Flatten the response header block into "Name: value\n" lines (status line
// dropped, CRs removed) so the JS layer can expose Headers /
// getAllResponseHeaders() without re-parsing CRLF on that side.
static void hb_capture_headers(const uint8_t* h, int hlen) {
    s_last_hdrs[0] = 0;
    if (!h || hlen <= 0) return;
    int p = 0;
    while (p < hlen && h[p] != '\n') p++;          // skip the status line
    int used = 0;
    while (p < hlen) {
        int ls = p + 1;
        if (ls >= hlen) break;
        int le = ls;
        while (le < hlen && h[le] != '\r' && h[le] != '\n') le++;
        int n = le - ls;
        if (n > 0 && used + n + 2 < (int)sizeof(s_last_hdrs)) {
            memcpy(s_last_hdrs + used, h + ls, (size_t)n);
            used += n;
            s_last_hdrs[used++] = '\n';
            s_last_hdrs[used] = 0;
        }
        p = le;
        if (p < hlen && h[p] == '\r') p++;
        if (p < hlen && h[p] == '\n') p++;
    }
}

// Response headers of the last completed request, "Name: value" per '\n'.
const char* httpbare_last_headers() { return s_last_hdrs; }

// decode a hex number (chunk sizes); stops at ';' or CR/LF
static uint32_t hb_hex(const char* s, int maxlen, int* used) {
    uint32_t v = 0;
    int i = 0;
    while (i < maxlen) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * 16 + (uint32_t)d;
        i++;
    }
    *used = i;
    return v;
}

// ---------------------------------------------------------------------------
// main request
// ---------------------------------------------------------------------------
static bool httpbare_request_once(const char* method, const char* url,
                      const char* extra_headers,
                      const uint8_t* body, uint32_t body_len,
                      int* out_status, char** out_headers, uint32_t* out_hlen,
                      uint8_t** out_body, uint32_t* out_len) {
    if (!url || !out_body || !out_len) return false;
    s_set_cookie[0] = 0;              // never leak the previous response's jar
    s_last_hdrs[0] = 0;               // ... nor its headers
    if (out_headers) *out_headers = 0;
    if (out_hlen) *out_hlen = 0;
    if (out_status) *out_status = 0;
    *out_body = 0;
    *out_len = 0;
    if (!method) method = "GET";

    // ---- parse URL ----
    const char* scheme = strstr(url, "://");
    if (!scheme) return false;
    bool https = (strncmp(url, "https", 5) == 0);
    const char* host = scheme + 3;
    const char* he = host;
    while (*he && *he != ':' && *he != '/') he++;
    int hlen = (int)(he - host);          // host name length (port excluded)
    int port = https ? 443 : 80;
    if (*he == ':') {
        const char* pe = he + 1;
        int pl = 0;
        while (pe[pl] >= '0' && pe[pl] <= '9') pl++;
        if (pl > 0 && pl <= 5) {
            int v = 0;
            for (int i = 0; i < pl; i++) v = v * 10 + (pe[i] - '0');
            if (v > 0) port = v;
        }
        he = pe + pl;                     // continue after the port
    }
    if (hlen <= 0 || hlen >= 128) return false;
    char hostname[128];
    memcpy(hostname, host, hlen);
    hostname[hlen] = 0;
    const char* path = he;
    if (!*path) path = "/";
    char reqpath[1024];
    {
        int pl = (int)strlen(path);
        if (pl <= 0 || pl >= (int)sizeof(reqpath)) return false;
        memcpy(reqpath, path, pl + 1);
    }
    klogf("hb: req %s %s%s\n", method ? method : "GET", hostname, reqpath);

    // ---- DNS + TCP ----
    // Note: hostname[] excludes the port (the parser above stops at ':'), so
    // the resolver always gets a bare host name.
    //
    // A CDN name resolves to several addresses and only some of them are
    // reachable from a given network, so every candidate is tried until one
    // accepts the connection.
    uint32_t ips[6];
    int nip = dns_lookup_multi(hostname, ips, 6);
    if (nip <= 0) {
        // Fall back to the single-shot resolver (static table / one query).
        uint32_t one = hb_dns_lookup(hostname);
        if (!one) return false;
        ips[0] = one;
        nip = 1;
    }
    int fd = -1;
    for (int i = 0; i < nip; i++) {
        fd = tcp_connect(ips[i], (uint16_t)port, 10000);
        if (fd >= 0) break;
        klogf("hb: connect %s:%d via %u.%u.%u.%u failed, trying next\n",
              hostname, port, (ips[i]>>24)&0xff, (ips[i]>>16)&0xff,
              (ips[i]>>8)&0xff, ips[i]&0xff);
    }
    if (fd < 0) return false;

    // ---- TLS (https) ----
    nefu::tls::Tls13Conn* tls = 0;
    if (https) {
        g_tls_fd = fd;
        char terr[160];
        tls = nefu::tls::tls13_connect(hb_tls_recv, hb_tls_send, 0,
                                       hostname, terr, sizeof(terr));
        if (!tls) {
            klogf("hb: TLS connect FAIL (%s:%d): %s\n", hostname, port, terr);
            tcp_close(fd);
            return false;
        }
        klogf("hb: TLS ok alpn=%s\n", nefu::tls::tls13_alpn(tls));
    }

    // ---- request line ----
    char req[3072];
    int rl = 0;
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "%s %s HTTP/1.1\r\n", method, reqpath);
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Host: %s", hostname);
    if (port != (https ? 443 : 80))
        rl += ksprintf(req + rl, (int)sizeof(req) - rl, ":%d", port);
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl,
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Accept: */*\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Connection: close\r\n");
    if (extra_headers) {
        int el = (int)strlen(extra_headers);
        if (rl + el + 2 < (int)sizeof(req)) {
            memcpy(req + rl, extra_headers, el);
            rl += el;
            if (el < 2 || extra_headers[el - 2] != '\r')
                rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
        } else {
            // Never drop headers silently: a missing Authorization/Content-Type
            // turns into an opaque 4xx and looks like a server-side rejection.
            klogf("hb: WARN extra headers dropped (%d bytes > %d room)\n",
                  el, (int)sizeof(req) - rl - 2);
        }
    }
    if (body && body_len > 0)
        rl += ksprintf(req + rl, (int)sizeof(req) - rl,
                       "Content-Length: %u\r\n", (unsigned)body_len);
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
    // Only inline the body when it fits in the stack buffer. Anything larger is
    // streamed right after the header block (see "send body" below): previously
    // the body was silently dropped while Content-Length still advertised the
    // full size, so the peer blocked waiting for bytes that never arrived
    // (every large POST - Firebase auth, matchmake options, analytics - hung
    // until the idle timeout).
    uint32_t body_embedded = 0;
    if (body && body_len > 0) {
        if (rl + (int)body_len <= (int)sizeof(req)) {
            memcpy(req + rl, body, body_len);
            rl += (int)body_len;
            body_embedded = body_len;
        }
    }

    // ---- send ----
    int sent = tls ? nefu::tls::tls13_send(tls, (const uint8_t*)req, rl)
                   : tcp_send(fd, req, rl);
    klogf("hb: sent %d/%d to %s:%d\n", sent, rl, hostname, port);
    if (sent != rl) {
        if (tls) nefu::tls::tls13_close(tls);
        tcp_close(fd);
        return false;
    }
    // Send the part of the body that did not fit in `req`. Chunked so a
    // multi-hundred-KB upload (schematic/texture POST) neither needs a huge
    // stack buffer nor trips the TLS layer's per-record limit.
    if (body && body_len > body_embedded) {
        uint32_t off = body_embedded;
        while (off < body_len) {
            uint32_t chunk = body_len - off;
            if (chunk > 8192) chunk = 8192;
            int s2 = tls ? nefu::tls::tls13_send(tls, body + off, (int)chunk)
                         : tcp_send(fd, (const char*)body + off, (int)chunk);
            if (s2 <= 0) {
                klogf("hb: body send FAIL at %u/%u\n", (unsigned)off, (unsigned)body_len);
                if (tls) nefu::tls::tls13_close(tls);
                tcp_close(fd);
                return false;
            }
            off += (uint32_t)s2;
        }
        klogf("hb: body streamed %u bytes (inline %u)\n",
              (unsigned)(body_len - body_embedded), (unsigned)body_embedded);
    }

    // ---- read headers ----
    HbBuf hb;
    // Read in TLS-record-sized chunks: the TLS layer hands over at most one
    // record (16 KB) per call, so a smaller buffer would need many more
    // round-trips through the decrypt/stash path for large assets.
    uint8_t tmp[16384];
    int hdr_end = -1;
    uint32_t start = platform_tick_ms();
    uint32_t hdr_deadline = start + HB_IDLE_TIMEOUT_MS;
    uint32_t iter = 0;
    while (hdr_end < 0) {
        if ((int32_t)(platform_tick_ms() - hdr_deadline) > 0) break;
        int r = hb_recv(tls, fd, tmp, (int)sizeof(tmp), 800);
        hb_beat("hdr", ++iter, hb.len, r);
        if (r == HB_R_TIMEOUT) continue;   // slow peer: keep polling until deadline
        if (r <= 0) break;                 // EOF or fatal error
        hdr_deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;   // progress
        if (!hb.append(tmp, (uint32_t)r)) { hb.free(); if (tls) nefu::tls::tls13_close(tls); tcp_close(fd); return false; }
        // header terminator: "\r\n\r\n" or "\n\n". Scan from the start of the
        // whole buffer: responses often arrive in a single recv, so a tail-only
        // search window (len - r) underflows and silently skips the scan.
        for (uint32_t i = 0; i + 3 < hb.len; i++) {
            if (hb.p[i] == '\r' && hb.p[i+1] == '\n' && hb.p[i+2] == '\r' && hb.p[i+3] == '\n') { hdr_end = (int)i + 4; break; }
        }
        if (hdr_end < 0) {
            for (uint32_t i = 0; i + 1 < hb.len; i++) {
                if (hb.p[i] == '\n' && hb.p[i+1] == '\n') { hdr_end = (int)i + 2; break; }
            }
        }
    }
    if (hdr_end < 0 || !hb.p) {
        klogf("hb: header read FAIL hdr_end=%d buffered=%u\n", hdr_end, hb.len);
        hb.free();
        if (tls) nefu::tls::tls13_close(tls);
        tcp_close(fd);
        return false;
    }

    // ---- status ----
    int status = 0;
    {
        // "HTTP/1.1 200 ..."
        int sp = 0;
        while (sp < hdr_end && hb.p[sp] != ' ') sp++;
        while (sp < hdr_end && hb.p[sp] == ' ') sp++;
        while (sp < hdr_end && hb.p[sp] >= '0' && hb.p[sp] <= '9') {
            status = status * 10 + (hb.p[sp] - '0');
            sp++;
        }
    }

    // Cookies must reach the document jar before the body is handed back, and
    // they are cheap to collect from the header block already in memory.
    hb_capture_set_cookie(hb.p, hdr_end);

    // ---- body ----
    HbBuf bb;
    bool body_ok = false;
    uint32_t body_start = (uint32_t)hdr_end;
    if (body_start < hb.len) {
        if (!bb.append(hb.p + body_start, hb.len - body_start)) { body_ok = false; }
        else body_ok = true;
    } else {
        body_ok = true;
    }
    int cl = hb_find_cl((const char*)hb.p, hdr_end);
    bool chunked = hb_is_chunked((const char*)hb.p, hdr_end);
    klogf("hb: body plan status=%d cl=%d chunked=%d initial=%u\n",
          status, cl, (int)chunked, (unsigned)bb.len);

    if (body_ok) {
        if (cl >= 0) {
            // read exactly cl bytes
            uint32_t deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;
            iter = 0;
            while (bb.len < (uint32_t)cl) {
                if ((int32_t)(platform_tick_ms() - deadline) > 0) { body_ok = false; break; }
                int r = hb_recv(tls, fd, tmp, (int)sizeof(tmp), 800);
                hb_beat("body-cl", ++iter, bb.len, r);
                if (r == HB_R_TIMEOUT) continue;    // slow peer: keep waiting
                if (r <= 0) { body_ok = false; break; }
                deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;   // progress
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
            if (bb.len < (uint32_t)cl) body_ok = false;
            else if (bb.len > (uint32_t)cl) bb.len = (uint32_t)cl;
        } else if (chunked) {
            // Chunked transfer (RFC 7230 4.1). The body ends with the
            // terminating 0-length chunk, NOT with the connection close, so
            // the stream is decoded incrementally and the read stops as soon
            // as the terminator arrives. Waiting for the close instead would
            // hang until the deadline on keep-alive servers.
            uint32_t deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;
            HbBuf dec;
            uint32_t i = 0;              // parse cursor into bb
            bool done = false;           // terminating chunk seen
            iter = 0;
            while (!done) {
                hb_beat("body-chunk", ++iter, bb.len, 0);
                // decode as many complete chunks as the buffer holds
                while (i < bb.len) {
                    // chunk size line: hex digits, optional ";ext", then CRLF.
                    // The line ends at the first LF; the CR (if any) is part of it.
                    uint32_t ls = i;
                    while (ls < bb.len && bb.p[ls] != '\n') ls++;
                    if (ls >= bb.len) break;                 // size line incomplete
                    int used = 0;
                    uint32_t sz = hb_hex((const char*)bb.p + i, (int)(ls - i), &used);
                    if (used <= 0) { body_ok = false; done = true; break; }
                    uint32_t after = ls + 1;                 // first byte after the size line
                    if (sz == 0) { i = after; done = true; break; }   // terminator chunk
                    if (after + sz > bb.len) break;           // chunk data incomplete
                    if (!dec.append(bb.p + after, sz)) { body_ok = false; done = true; break; }
                    i = after + sz;
                    // skip the CRLF that terminates the chunk data
                    while (i < bb.len && (bb.p[i] == '\r' || bb.p[i] == '\n')) i++;
                }
                if (done) break;
                if (!body_ok) break;
                if ((int32_t)(platform_tick_ms() - deadline) > 0) { body_ok = false; break; }
                int r = hb_recv(tls, fd, tmp, (int)sizeof(tmp), 800);
                if (r == HB_R_TIMEOUT) continue;              // slow peer: keep waiting
                if (r == 0) {
                    // Peer closed without sending the terminating 0-length
                    // chunk.  CDNs do this on keep-alive connections whose
                    // final small record was lost / not ACKed in time.  A real
                    // browser accepts the bytes it already has; do the same,
                    // otherwise a fully received page is discarded.
                    if (dec.len > 0) done = true;
                    else body_ok = false;
                    break;
                }
                if (r < 0) { body_ok = false; break; }         // fatal
                deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;   // progress
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
            if (body_ok && !done) body_ok = false;             // terminator never arrived
            bb.free();
            bb = dec;
        } else {
            // read until close
            uint32_t deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;
            iter = 0;
            for (;;) {
                if ((int32_t)(platform_tick_ms() - deadline) > 0) break;
                int r = hb_recv(tls, fd, tmp, (int)sizeof(tmp), 800);
                hb_beat("body-eof", ++iter, bb.len, r);
                if (r == HB_R_TIMEOUT) continue;    // slow peer: keep waiting
                if (r <= 0) break;                   // EOF (0) or fatal
                deadline = platform_tick_ms() + HB_IDLE_TIMEOUT_MS;   // progress
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
        }
    }

    // hand the header block to the caller so it can inspect Content-Encoding
    // and Location (redirects) without re-parsing the whole response.
    char* hcopy = (char*)kalloc(hdr_end + 1);
    if (hcopy) { memcpy(hcopy, hb.p, hdr_end); hcopy[hdr_end] = 0; }
    hb.free();
    if (tls) nefu::tls::tls13_close(tls);
    tcp_close(fd);

    if (!body_ok) {
        klogf("hb: body read FAIL status=%d cl=%d chunked=%d have=%u\n",
              status, cl, (int)chunked, bb.len);
        bb.free();
        if (hcopy) kfree(hcopy);
        return false;
    }
    klogf("hb: OK status=%d body=%u\n", status, (unsigned)bb.len);
    // Cross-layer integrity check: nfu_mod_loader prints the same FNV-1a of
    // the bytes it is about to compile. If the two disagree, the body was
    // overwritten AFTER httpbare_request_once returned (allocator / lifetime
    // bug). If they agree but QuickJS still reports a SyntaxError, the bytes
    // were already wrong on the wire (TCP reassembly / TLS record boundary).
    if (bb.p && bb.len) {
        uint32_t hv = 2166136261u;
        for (uint32_t i2 = 0; i2 < bb.len; i2++) { hv ^= bb.p[i2]; hv *= 16777619u; }
        klogf("hb: bodyfnv=%08x len=%u\n", (unsigned)hv, (unsigned)bb.len);
    }
    // Diagnostic for the intermittent "body length is exactly Content-Length
    // but QuickJS reports a SyntaxError" failure: show whether the first bytes
    // of the body are clean ASCII (a TLS handshake leftover / recv corruption
    // would show up here as control bytes) so the next bare-metal run can tell
    // corruption at the head from corruption in the middle of the stream.
    if (bb.p && bb.len >= 8) {
        klogf("hb: body0=%02x%02x%02x%02x%02x%02x%02x%02x\n",
              bb.p[0], bb.p[1], bb.p[2], bb.p[3],
              bb.p[4], bb.p[5], bb.p[6], bb.p[7]);
    }
    // NUL-terminate the body: QuickJS's lexer reads one byte past the length
    // as an EOF sentinel, so the spare byte that HbBuf always keeps must be a
    // predictable 0 instead of uninitialised garbage (random SyntaxErrors on
    // byte-exact downloads).  Callers that only use the length are unaffected.
    if (bb.p) bb.p[bb.len] = 0;
    if (out_status) *out_status = status;
    if (out_headers) *out_headers = hcopy;
    if (out_hlen) *out_hlen = (uint32_t)hdr_end;
    *out_body = bb.p ? bb.p : 0;
    *out_len = bb.len;
    return true;
}

// Resolve a possibly-relative Location header against the request base URL.
// Writes the absolute URL into out (outcap bytes). Returns true on success.
static bool hb_resolve_url(const char* base, const char* loc, int loclen,
                           char* out, int outcap) {
    if (loclen <= 0 || outcap <= 0) return false;
    if (loclen >= 7 && strncmp(loc, "http://", 7) == 0) {
        if (loclen + 1 > outcap) return false;
        memcpy(out, loc, loclen); out[loclen] = 0; return true;
    }
    if (loclen >= 8 && strncmp(loc, "https://", 8) == 0) {
        if (loclen + 1 > outcap) return false;
        memcpy(out, loc, loclen); out[loclen] = 0; return true;
    }
    const char* sch = strstr(base, "://");
    if (!sch) return false;
    int schlen = (int)(sch - base);
    const char* auth = sch + 3;
    const char* aend = auth;
    while (*aend && *aend != '/' && *aend != '?' && *aend != '#') aend++;
    int authlen = (int)(aend - auth);
    if (loclen >= 2 && loc[0] == '/' && loc[1] == '/') {
        int need = schlen + 3 + (loclen - 2) + 1;
        if (need > outcap) return false;
        int o = 0;
        memcpy(out, base, schlen); o += schlen;
        out[o++] = ':'; out[o++] = '/'; out[o++] = '/';
        memcpy(out + o, loc + 2, loclen - 2); o += (loclen - 2);
        out[o] = 0; return true;
    }
    if (loc[0] == '/') {
        int need = schlen + 3 + authlen + loclen + 1;
        if (need > outcap) return false;
        int o = 0;
        memcpy(out, base, schlen); o += schlen;
        out[o++] = ':'; out[o++] = '/'; out[o++] = '/';
        memcpy(out + o, auth, authlen); o += authlen;
        memcpy(out + o, loc, loclen); o += loclen;
        out[o] = 0; return true;
    }
    const char* lastslash = aend;
    for (const char* p = aend; *p; p++) if (*p == '/') lastslash = p;
    int basepathlen = (int)(lastslash - base);
    int need = (lastslash == aend && loc[0] != '/')
                  ? basepathlen + 1 + loclen + 1
                  : basepathlen + loclen + 1;
    if (need > outcap) return false;
    int o = 0;
    memcpy(out, base, basepathlen); o += basepathlen;
    if (lastslash == aend && loc[0] != '/') out[o++] = '/';
    memcpy(out + o, loc, loclen); o += loclen;
    out[o] = 0;
    return true;
}

// Core fetch. `require_2xx` selects between the two public entry points:
// httpbare_request() fails on a non-2xx status (the classic "did the call
// succeed" answer), while httpbare_fetch() hands the body back whatever the
// status was - which is what a browser needs to render an error page.
static bool httpbare_fetch_impl(const char* method, const char* url,
                      const char* extra_headers,
                      const uint8_t* body, uint32_t body_len,
                      int* out_status, uint8_t** out_body, uint32_t* out_len,
                      bool require_2xx) {
    if (!out_body || !out_len) return false;
    *out_body = 0; *out_len = 0; if (out_status) *out_status = 0;
    if (!url) return false;

    char cur_url[2048];
    size_t ulen = strlen(url);
    if (ulen >= sizeof(cur_url)) return false;
    memcpy(cur_url, url, ulen + 1);

    const char* cur_method = method ? method : "GET";
    const uint8_t* cur_body = body;
    uint32_t cur_body_len = body_len;

    int status = 0;
    char* hdr = 0; uint32_t hdr_len = 0;
    uint8_t* raw = 0; uint32_t raw_len = 0;
    bool ok = false;

    for (int redir = 0; redir <= 8; redir++) {
        if (hdr) { kfree(hdr); hdr = 0; }
        if (raw) { kfree(raw); raw = 0; }
        if (!httpbare_request_once(cur_method, cur_url, extra_headers,
                                   cur_body, cur_body_len,
                                   &status, &hdr, &hdr_len, &raw, &raw_len)) {
            break;
        }
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            int llen = 0;
            const char* loc = hb_find_hdr(hdr, (int)hdr_len, "Location", &llen);
            if (loc && llen > 0 && redir < 8) {
                char nue[2048];
                if (hb_resolve_url(cur_url, loc, llen, nue, (int)sizeof(nue))) {
                    if (status == 303 || (status != 307 && status != 308)) {
                        cur_method = "GET";
                        cur_body = 0; cur_body_len = 0;
                    }
                    size_t nl = strlen(nue);
                    if (nl < sizeof(cur_url)) { memcpy(cur_url, nue, nl + 1); continue; }
                }
            }
        }
        ok = (status >= 200 && status < 300);
        break;
    }

    // Header block of the final hop (after redirects) belongs to the response
    // the caller is about to see - capture it before the early returns below
    // free it. This is what Headers / getAllResponseHeaders() report.
    hb_capture_headers((const uint8_t*)hdr, (int)hdr_len);

    if (!ok && require_2xx) {
        if (hdr) kfree(hdr);
        if (raw) kfree(raw);
        return false;
    }

    // Content-Encoding: gzip / x-gzip / deflate -> inflate in place.
    int enclen = 0;
    const char* enc = hb_find_hdr(hdr, (int)hdr_len, "Content-Encoding", &enclen);
    if (enc && enclen > 0 && raw_len > 0) {
        bool do_dec = false;
        for (int i = 0; i + 4 <= enclen; i++) {
            if (strncmp(enc + i, "gzip", 4) == 0 ||
                strncmp(enc + i, "x-gzip", 6) == 0) do_dec = true;
            if (strncmp(enc + i, "deflate", 7) == 0) do_dec = true;
        }
        if (do_dec) {
            uint32_t cap = raw_len * 8;
            if (cap < 65536) cap = 65536;
            const uint32_t MAXCAP = 64UL * 1024 * 1024;
            uint8_t* dec = 0;
            long rc = -1;
            while (cap <= MAXCAP) {
                // +1 spare byte: the caller NUL-terminates the decompressed
                // body in place, so the allocation must exceed the inflate
                // output bound by at least one byte.
                uint8_t* d = (uint8_t*)kalloc(cap + 1);
                if (!d) break;
                rc = nefu_http_inflate_into(d, cap, raw, raw_len);
                if (rc >= 0) { dec = d; break; }
                kfree(d);
                if ((rc == -5 || rc == -12) && cap < MAXCAP) { cap *= 2; continue; }
                break;
            }
            if (dec && rc >= 0) {
                dec[rc] = 0;   // same EOF-sentinel contract as the plain path
                kfree(raw); raw = dec; raw_len = (uint32_t)rc;
            } else {
                klogf("hb: decompress FAIL status=%d enc=%.*s rc=%ld\n",
                      status, enclen, enc, rc);
                kfree(hdr); kfree(raw);
                return false;
            }
        }
    }

    kfree(hdr);
    if (out_status) *out_status = status;
    *out_body = raw;
    *out_len = raw_len;
    return true;
}

bool httpbare_request(const char* method, const char* url,
                      const char* extra_headers,
                      const uint8_t* body, uint32_t body_len,
                      int* out_status, uint8_t** out_body, uint32_t* out_len) {
    return httpbare_fetch_impl(method, url, extra_headers, body, body_len,
                               out_status, out_body, out_len, true);
}

bool httpbare_fetch(const char* method, const char* url,
                    const char* extra_headers,
                    const uint8_t* body, uint32_t body_len,
                    int* out_status, uint8_t** out_body, uint32_t* out_len) {
    return httpbare_fetch_impl(method, url, extra_headers, body, body_len,
                               out_status, out_body, out_len, false);
}

} // namespace net
} // namespace nefu

