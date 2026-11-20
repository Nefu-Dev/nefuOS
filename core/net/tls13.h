/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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

// The recv callback must report the three outcomes distinctly:
//   > 0 : bytes received into buf
//     0 : peer closed the connection (clean EOF)
//    -1 : no data available within timeout_ms (retry later; nothing consumed)
//    -2 : fatal socket error
// Distinguishing -1 from 0 matters: a slow peer must not be mistaken for a
// closed connection, otherwise the response is truncated mid-transfer.
struct Tls13Conn;

// Connect: sends ClientHello, drives the handshake to the application
// data phase. err/errsz receive a human-readable failure reason.
Tls13Conn* tls13_connect(tls_recv_fn rf, tls_send_fn sf, void* ctx,
                         const char* hostname,
                         char* err, int errsz);

// Post-handshake application data (encrypted).
int tls13_send(Tls13Conn* c, const uint8_t* data, int len);

// Read decrypted application data.
//   > 0 : bytes written into buf
//     0 : peer closed the connection (clean EOF)
//    -1 : no data available yet (socket timeout) - the caller may call again;
//         the connection state, including a partially received TLS record, is
//         preserved so the transfer resumes exactly where it stopped
//    -2 : fatal error (protocol violation, decryption failure, lost socket)
int tls13_recv(Tls13Conn* c, uint8_t* buf, int maxlen, int timeout_ms);
void tls13_close(Tls13Conn* c);

// After a successful handshake, returns the ALPN protocol the server selected
// ("" if the server sent none). The ClientHello advertises only "http/1.1"
// (see build_client_hello): this client speaks HTTP/1.1 exclusively, so
// advertising "h2" would risk the server switching to a binary framing we
// cannot parse.
const char* tls13_alpn(Tls13Conn* c);

} // namespace tls
} // namespace nefu

