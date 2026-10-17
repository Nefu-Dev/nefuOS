// nefuOS HTTP/2.0 client subset (RFC 7540 / RFC 7541)
//
// A self-contained HTTP/2 client implementing the subset needed for
// https:// browsing:
//   - connection preface + SETTINGS + one GET request on stream 1
//   - HPACK request encoding  (static table + literal, no Huffman)
//   - HPACK response decoding (static table + dynamic table + Huffman)
//   - HEADERS/CONTINUATION/DATA/SETTINGS/PING/WINDOW_UPDATE handling
//   - GOAWAY / RST_STREAM / protocol errors surfaced as failures
// Transport-agnostic: reads and writes go through recv/send callbacks, so
// it runs on both the win32 host (WinSock + native TLS 1.3) and the
// bare-metal stack. The caller MUST only use it after TLS ALPN negotiated
// "h2".
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace http2 {

// Decoded response of one HTTP/2 stream.
// body is allocated with kalloc(); the caller must kfree(body) when done.
// headers[] stores "Name: value" lines (no CRLF, no :status pseudo-header).
struct Http2Response {
    int status;                     // :status (0 if missing)
    char status_text[64];           // reason phrase if provided, else ""
    char headers[32][128];
    int header_count;
    uint8_t* body;
    int body_len;
    int content_length;             // -1 if unknown
    bool end_stream_seen;

    Http2Response()
        : status(0), header_count(0), body(0), body_len(0),
          content_length(-1), end_stream_seen(false) {
        status_text[0] = 0;
        for (int i = 0; i < 32; i++) headers[i][0] = 0;
    }

    const char* get_header(const char* name) {
        size_t nl = 0;
        while (name[nl]) nl++;
        for (int i = 0; i < header_count; i++) {
            if (str_ncasecmp_ci(headers[i], name, nl) == 0) {
                const char* v = headers[i];
                while (*v && *v != ':') v++;
                if (*v == ':') v++;
                while (*v == ' ') v++;
                return v;
            }
        }
        return "";
    }
private:
    static int str_ncasecmp_ci(const char* a, const char* b, size_t n) {
        for (size_t i = 0; i < n; i++) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y) return (int)(unsigned char)x - (int)(unsigned char)y;
        }
        return 0;
    }
};

typedef int (*h2_recv_fn)(void* ctx, uint8_t* buf, int maxlen, int timeout_ms);
typedef int (*h2_send_fn)(void* ctx, const uint8_t* buf, int len);

// Perform one HTTP/2 GET over an established, ALPN-negotiated h2 transport.
// Sends the connection preface, SETTINGS and the HEADERS frame (stream 1,
// END_STREAM), then reads frames until the stream ends (or fails).
// On success returns a new Http2Response (kfree its body, delete it).
// On failure returns 0 and err/errsz receive a short reason.
Http2Response* http2_get(h2_recv_fn rf, h2_send_fn sf, void* ctx,
                         const char* authority, const char* path,
                         char* err, int errsz);
// Diagnostics: decode an HPACK block into a fresh response (0 ok, -1 error).
int http2_test_decode(const uint8_t* data, int len, Http2Response* r);

} // namespace http2
} // namespace nefu
