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
#include <string.h>

namespace nefu {
namespace net {

// ---------------------------------------------------------------------------
// static DNS table (the bare NIC path has no live resolver wired yet)
// ---------------------------------------------------------------------------
static uint32_t hb_dns_lookup(const char* name) {
    static const struct { const char* n; uint32_t ip; } tbl[] = {
        { "localhost",      0x7F000001u },
        { "npkg.site.je",   185u<<24 | 27u<<16 | 134u<<8 | 202u },
        { "nefuos.site.je", 185u<<24 | 27u<<16 | 134u<<8 | 202u },
        { "gateway",        10u<<24 | 0u<<16 | 2u<<8 | 2u },
        { "nefuos.dev",     10u<<24 | 0u<<16 | 0u<<8 | 2u },
    };
    for (unsigned i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (strcmp(tbl[i].n, name) == 0) return tbl[i].ip;
    }
    return 0;
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
        if (len + n > cap) {
            uint32_t ncap = cap ? cap : 1024;
            while (ncap < len + n) ncap *= 2;
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

// ---------------------------------------------------------------------------
// TLS bridge onto the bare TCP stack
// ---------------------------------------------------------------------------
static int g_tls_fd = -1;
static int hb_tls_recv(void* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    (void)ctx;
    return tcp_recv(g_tls_fd, buf, maxlen, timeout_ms);
}
static int hb_tls_send(void* ctx, const uint8_t* buf, int len) {
    (void)ctx;
    return tcp_send(g_tls_fd, buf, len);
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
    for (int i = 0; i + 6 <= vlen; i++) {
        if (strncmp(v + i, "chunked", 7) == 0) return true;
    }
    return false;
}

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
bool httpbare_request(const char* method, const char* url,
                      const char* extra_headers,
                      const uint8_t* body, uint32_t body_len,
                      int* out_status, uint8_t** out_body, uint32_t* out_len) {
    if (!url || !out_body || !out_len) return false;
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
        he = pe + pl;
    }
    int hlen = (int)(he - host);
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

    // ---- DNS ----
    uint32_t ip = hb_dns_lookup(hostname);
    if (!ip) return false;

    // ---- TCP ----
    int fd = tcp_connect(ip, (uint16_t)port, 10000);
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
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36 nefuOS/0.2\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Accept: */*\r\n");
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "Connection: close\r\n");
    if (extra_headers) {
        int el = (int)strlen(extra_headers);
        if (rl + el + 2 < (int)sizeof(req)) {
            memcpy(req + rl, extra_headers, el);
            rl += el;
            if (el < 2 || extra_headers[el - 2] != '\r')
                rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
        }
    }
    if (body && body_len > 0)
        rl += ksprintf(req + rl, (int)sizeof(req) - rl,
                       "Content-Length: %u\r\n", (unsigned)body_len);
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
    if (body && body_len > 0) {
        if (rl + (int)body_len <= (int)sizeof(req)) {
            memcpy(req + rl, body, body_len);
            rl += (int)body_len;
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

    // ---- read headers ----
    HbBuf hb;
    uint8_t tmp[2048];
    int hdr_end = -1;
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < 15000 && hdr_end < 0) {
        int r = tls ? nefu::tls::tls13_recv(tls, tmp, (int)sizeof(tmp), 800)
                    : tcp_recv(fd, tmp, (int)sizeof(tmp), 800);
        if (r <= 0) {
            if (r == 0) break;             // clean close (rare before headers)
            continue;                      // timeout: keep polling until deadline
        }
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

    if (body_ok) {
        if (cl >= 0) {
            // read exactly cl bytes
            uint32_t deadline = platform_tick_ms() + 15000;
            while (bb.len < (uint32_t)cl) {
                if ((int32_t)(platform_tick_ms() - deadline) > 0) { body_ok = false; break; }
                int r = tls ? nefu::tls::tls13_recv(tls, tmp, (int)sizeof(tmp), 800)
                            : tcp_recv(fd, tmp, (int)sizeof(tmp), 800);
                if (r <= 0) break;
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
            if (bb.len < (uint32_t)cl) body_ok = false;
            else if (bb.len > (uint32_t)cl) bb.len = (uint32_t)cl;
        } else if (chunked) {
            // read until close, then decode chunks
            uint32_t deadline = platform_tick_ms() + 15000;
            for (;;) {
                if ((int32_t)(platform_tick_ms() - deadline) > 0) break;
                int r = tls ? nefu::tls::tls13_recv(tls, tmp, (int)sizeof(tmp), 800)
                            : tcp_recv(fd, tmp, (int)sizeof(tmp), 800);
                if (r <= 0) break;
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
            // decode chunked stream in place
            HbBuf dec;
            uint32_t i = 0;
            body_ok = true;
            while (i < bb.len) {
                int used = 0;
                uint32_t sz = hb_hex((const char*)bb.p + i, (int)(bb.len - i), &used);
                i += (uint32_t)used;
                while (i < bb.len && (bb.p[i] == '\r' || bb.p[i] == '\n')) i++;
                if (sz == 0) break;
                if (i + sz > bb.len) { body_ok = false; break; }
                if (!dec.append(bb.p + i, sz)) { body_ok = false; break; }
                i += sz;
                while (i < bb.len && (bb.p[i] == '\r' || bb.p[i] == '\n')) i++;
            }
            bb.free();
            bb = dec;
        } else {
            // read until close
            uint32_t deadline = platform_tick_ms() + 15000;
            for (;;) {
                if ((int32_t)(platform_tick_ms() - deadline) > 0) break;
                int r = tls ? nefu::tls::tls13_recv(tls, tmp, (int)sizeof(tmp), 800)
                            : tcp_recv(fd, tmp, (int)sizeof(tmp), 800);
                if (r <= 0) break;
                if (!bb.append(tmp, (uint32_t)r)) { body_ok = false; break; }
            }
        }
    }

    hb.free();
    if (tls) nefu::tls::tls13_close(tls);
    tcp_close(fd);

    if (!body_ok) {
        klogf("hb: body read FAIL status=%d cl=%d chunked=%d have=%u\n",
              status, cl, (int)chunked, bb.len);
        bb.free();
        return false;
    }
    klogf("hb: OK status=%d body=%u\n", status, (unsigned)bb.len);
    if (out_status) *out_status = status;
    *out_body = bb.p ? bb.p : 0;
    *out_len = bb.len;
    return true;
}

} // namespace net
} // namespace nefu
