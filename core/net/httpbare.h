/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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

// Same exchange, but the body is returned whatever the status code was.
// A browser has to render a 404 page instead of reporting "no connection",
// so the streaming entry point uses this variant.
bool httpbare_fetch(const char* method, const char* url,
                    const char* extra_headers,
                    const uint8_t* body, uint32_t body_len,
                    int* out_status, uint8_t** out_body, uint32_t* out_len);

// Cookies set by the most recent completed exchange, one raw
// "name=value; attrs" line per '\n' (empty when the response carried no
// Set-Cookie). The pointer stays valid until the next request. The JS document
// jar replays each line through its normal setter, which strips the attributes
// and honours "Max-Age=0" as a deletion; sending them back is the caller's job.
const char* httpbare_last_set_cookie();

// Response headers of the most recent completed exchange, flattened to
// "Name: value\n" lines (status line and bare CRs removed, original casing and
// order kept). Empty when the exchange failed. Same lifetime rule as above.
const char* httpbare_last_headers();

} // namespace net
} // namespace nefu

