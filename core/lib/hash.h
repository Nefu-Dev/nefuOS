// nefuOS hash & encoding library
// md5, sha1, crc32, adler32, base64, hex — portable, no STL.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace hash {

// ---------- md5 ----------
struct MD5 {
    uint32_t a, b, c, d;
    uint64_t len;         // bytes processed
    uint8_t  buf[64];
    int      buf_len;
    MD5() { init(); }
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[16]);       // binary digest
    void hex_final(char out[33]);      // 32 hex chars + NUL
};

// ---------- sha1 ----------
struct SHA1 {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    int      buf_len;
    SHA1() { init(); }
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[20]);
    void hex_final(char out[41]);
};

// ---------- crc32 (IEEE 802.3, poly 0xEDB88320) ----------
uint32_t crc32(const void* data, size_t n, uint32_t seed = 0);
// ---------- adler32 ----------
uint32_t adler32(const void* data, size_t n, uint32_t seed = 1);

// ---------- base64 ----------
int base64_encode(const uint8_t* in, size_t in_len, char* out, size_t out_cap);
int base64_decode(const char* in, uint8_t* out, size_t out_cap);

// ---------- hex ----------
void hex_encode(const uint8_t* in, size_t n, char* out, bool upper = false);
int  hex_decode(const char* in, uint8_t* out, size_t out_cap);

} // namespace hash
} // namespace nefu
