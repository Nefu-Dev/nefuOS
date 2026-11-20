/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// core/net/gunzip.h
//
// Minimal HTTP Content-Encoding decompression helper for the bare-metal HTTP
// client.  Servers almost always compress text assets (HTML/JS/CSS) with
// gzip or zlib-wrapped deflate, so the client MUST decode them or the JS
// bundle arrives as opaque bytes and QuickJS fails to parse it.
//
// backends/bare/inflate.cpp provides nefu_inflate() -- a raw-DEFLATE (RFC 1951)
// decoder.  This header strips the gzip (RFC 1952) or zlib (RFC 1950) wrapper
// and feeds the raw DEFLATE stream to nefu_inflate.
//
// The helper does NOT allocate: the caller supplies the destination buffer and
// is responsible for growing it on the -5/-12 "output overflow" return codes.
// That keeps this header free of any kalloc/malloc dependency so it links into
// both the bare kernel and the host test harness.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// raw-DEFLATE decoder (RFC 1951); returns byte count or a negative error code.
long nefu_inflate(unsigned char* dst, unsigned long dst_cap,
                  const unsigned char* src, unsigned long src_len);
#ifdef __cplusplus
}
#endif

// Strip the gzip/zlib wrapper from an HTTP transfer-encoded body and inflate
// the embedded raw DEFLATE stream into dst (dst_cap bytes).
//
// Returns the number of decompressed bytes written (>= 0) on success, or a
// negative error code from nefu_inflate:
//   -1  input too short / unknown wrapper
//   -5  output overflow (literal/copy)   -> retry with a larger dst
//   -12 output buffer would overflow     -> retry with a larger dst
//   other negative codes indicate a corrupt stream.
static inline long nefu_http_inflate_into(unsigned char* dst, unsigned long dst_cap,
                                          const unsigned char* src, unsigned long src_len) {
    if (src_len < 2) return -1;
    const unsigned char* p = src;
    unsigned long n = src_len;

    if (p[0] == 0x1f && p[1] == 0x8b) {
        // gzip (RFC 1952): 10-byte fixed header + optional fields + trailer.
        if (n < 10) return -1;
        unsigned char flg = p[3];
        unsigned long off = 10;
        if (flg & 0x04) {                                   // FEXTRA
            if (n < 12) return -1;
            unsigned long xlen = (unsigned long)p[10] | ((unsigned long)p[11] << 8);
            off = 12 + xlen;
        }
        if (flg & 0x08) { while (off < n && p[off]) off++; off++; }  // FNAME
        if (flg & 0x10) { while (off < n && p[off]) off++; off++; }  // FCOMMENT
        if (flg & 0x02) { off += 2; }                                 // FHCRC
        if (off > n) return -1;
        p += off; n -= off;
    } else {
        // zlib/deflate (RFC 1950): 2-byte header (CM=8, valid checksum).
        unsigned long h = ((unsigned long)p[0] << 8) | p[1];
        if ((p[0] & 0x0f) == 8 && (h % 31) == 0) {
            if (n < 2) return -1;
            p += 2; n -= 2;
        }
        // else: assume raw DEFLATE with no wrapper (rare for HTTP).
    }
    return nefu_inflate(dst, dst_cap, p, n);
}

