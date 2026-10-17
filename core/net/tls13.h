// nefuOS native TLS 1.3 client (RFC 8446)
// - X25519 (RFC 7748) key exchange
// - AES-128-GCM + ChaCha20-Poly1305 AEAD
// - HKDF-SHA256 key schedule
// - Chrome-style ClientHello fingerprint (GREASE / ALPN h2 / suites order)
// - no certificate chain validation (NoVerify mode)
// Socket-agnostic: operates through recv/send callbacks, usable on both
// the win32 host (WinSock) and the bare-metal TCP stack.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace tls {

// cipher suite ids (TLS 1.3)
enum {
    TLS_AES_128_GCM_SHA256 = 0x1301,
    TLS_AES_256_GCM_SHA384 = 0x1302,
    TLS_CHACHA20_POLY1305_SHA256 = 0x1303,
};

typedef int (*tls_recv_fn)(void* ctx, uint8_t* buf, int maxlen, int timeout_ms);
typedef int (*tls_send_fn)(void* ctx, const uint8_t* buf, int len);

struct Tls13Conn;

// Connect: sends ClientHello, drives the handshake to the application
// data phase. err/errsz receive a human-readable failure reason.
Tls13Conn* tls13_connect(tls_recv_fn rf, tls_send_fn sf, void* ctx,
                         const char* hostname,
                         char* err, int errsz);

// Post-handshake application data (encrypted).
int tls13_send(Tls13Conn* c, const uint8_t* data, int len);
int tls13_recv(Tls13Conn* c, uint8_t* buf, int maxlen, int timeout_ms);
void tls13_close(Tls13Conn* c);

// After a successful handshake, returns the ALPN protocol the server selected
// ("" if the server sent none). The ClientHello advertises "h2, http/1.1", so
// a conforming server returns one of the two.
const char* tls13_alpn(Tls13Conn* c);

} // namespace tls
} // namespace nefu
