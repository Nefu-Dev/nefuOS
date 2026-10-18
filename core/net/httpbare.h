// core/net/httpbare.h
//
// Bare-metal HTTP(S) client for nefuOS.
//
// One-shot request helper that runs on the in-house TCP stack (core/net/net.cpp)
// with the native TLS 1.3 client (core/net/tls13.cpp) for https:// URLs.
// DNS is a static host table (the bare stack has no live DNS resolver wired
// into the NIC path yet). Supports the npkg slowAES challenge flow through the
// extra_headers parameter (a `Cookie: __test=...` line).
//
// No STL, no exceptions, no heap beyond kalloc/kfree.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace net {

// One-shot HTTP(S) request.
//   method        : "GET" / "POST"
//   url           : "http://host[:port]/path" or "https://host[:port]/path"
//   extra_headers : extra request header lines (may be 0); each line must end
//                   with "\r\n" (e.g. npkg's "Cookie: __test=...\r\n")
//   body/body_len : request body for POST (0 for GET)
//   out_status    : HTTP status code (0 on failure)
//   out_body      : kalloc'd response body (caller must kfree); 0 on failure
//   out_len       : response body length
// Returns true on a completed HTTP exchange (any status code), false on
// network/TLS/protocol failure.
bool httpbare_request(const char* method, const char* url,
                      const char* extra_headers,
                      const uint8_t* body, uint32_t body_len,
                      int* out_status, uint8_t** out_body, uint32_t* out_len);

} // namespace net
} // namespace nefu
