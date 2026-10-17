// nefuOS HTTP/2.0 client subset (RFC 7540 / RFC 7541) - implementation
// - connection preface + SETTINGS + GET on stream 1
// - HPACK request encoding (static table + literal, no Huffman)
// - HPACK response decoding (static table + dynamic table + Huffman)
// - flow control: advertise 4 MiB window, replenish as DATA arrives
#include "http2.h"
#include "../platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>

namespace nefu {
namespace http2 {

// ============================================================
// Big-endian helpers
// ============================================================
static void put16(uint8_t* p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put24(uint8_t* p, int v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }

// ============================================================
// RFC 7541 Appendix A: static table (61 entries)
// ============================================================
struct StaticEntry { const char* name; const char* value; };
static const StaticEntry kStaticTable[61] = {
    {":authority", ""}, {":method", "GET"}, {":method", "POST"}, {":path", "/"},
    {":path", "/index.html"}, {":scheme", "http"}, {":scheme", "https"},
    {":status", "200"}, {":status", "204"}, {":status", "206"}, {":status", "304"},
    {":status", "400"}, {":status", "404"}, {":status", "500"},
    {"accept-charset", ""}, {"accept-encoding", "gzip, deflate"}, {"accept-language", ""},
    {"accept-ranges", ""}, {"accept", ""}, {"access-control-allow-origin", ""},
    {"age", ""}, {"allow", ""}, {"authorization", ""}, {"cache-control", ""},
    {"content-disposition", ""}, {"content-encoding", ""}, {"content-language", ""},
    {"content-length", ""}, {"content-location", ""}, {"content-range", ""},
    {"content-type", ""}, {"cookie", ""}, {"date", ""}, {"etag", ""}, {"expect", ""},
    {"expires", ""}, {"from", ""}, {"host", ""}, {"if-match", ""},
    {"if-modified-since", ""}, {"if-none-match", ""}, {"if-range", ""},
    {"if-unmodified-since", ""}, {"last-modified", ""}, {"link", ""}, {"location", ""},
    {"max-forwards", ""}, {"proxy-authenticate", ""}, {"proxy-authorization", ""},
    {"range", ""}, {"referer", ""}, {"refresh", ""}, {"retry-after", ""},
    {"server", ""}, {"set-cookie", ""}, {"strict-transport-security", ""},
    {"transfer-encoding", ""}, {"user-agent", ""}, {"vary", ""}, {"via", ""},
    {"www-authenticate", ""}
};

// ============================================================
// RFC 7541 Appendix B: Huffman code (parsed from the RFC text)
// ============================================================
struct HuffCode { uint32_t code; uint8_t bits; };
static const HuffCode kHuffmanCodes[257] = {
    {0x00001ff8u,13}, {0x007fffd8u,23}, {0x0fffffe2u,28}, {0x0fffffe3u,28}, {0x0fffffe4u,28},
    {0x0fffffe5u,28}, {0x0fffffe6u,28}, {0x0fffffe7u,28}, {0x0fffffe8u,28}, {0x00ffffeau,24},
    {0x3ffffffcu,30}, {0x0fffffe9u,28}, {0x0fffffeau,28}, {0x3ffffffdu,30}, {0x0fffffebu,28},
    {0x0fffffecu,28}, {0x0fffffedu,28}, {0x0fffffeeu,28}, {0x0fffffefu,28}, {0x0ffffff0u,28},
    {0x0ffffff1u,28}, {0x0ffffff2u,28}, {0x3ffffffeu,30}, {0x0ffffff3u,28}, {0x0ffffff4u,28},
    {0x0ffffff5u,28}, {0x0ffffff6u,28}, {0x0ffffff7u,28}, {0x0ffffff8u,28}, {0x0ffffff9u,28},
    {0x0ffffffau,28}, {0x0ffffffbu,28}, {0x00000014u,6}, {0x000003f8u,10}, {0x000003f9u,10},
    {0x00000ffau,12}, {0x00001ff9u,13}, {0x00000015u,6}, {0x000000f8u,8}, {0x000007fau,11},
    {0x000003fau,10}, {0x000003fbu,10}, {0x000000f9u,8}, {0x000007fbu,11}, {0x000000fau,8},
    {0x00000016u,6}, {0x00000017u,6}, {0x00000018u,6}, {0x00000000u,5}, {0x00000001u,5},
    {0x00000002u,5}, {0x00000019u,6}, {0x0000001au,6}, {0x0000001bu,6}, {0x0000001cu,6},
    {0x0000001du,6}, {0x0000001eu,6}, {0x0000001fu,6}, {0x0000005cu,7}, {0x000000fbu,8},
    {0x00007ffcu,15}, {0x00000020u,6}, {0x00000ffbu,12}, {0x000003fcu,10}, {0x00001ffau,13},
    {0x00000021u,6}, {0x0000005du,7}, {0x0000005eu,7}, {0x0000005fu,7}, {0x00000060u,7},
    {0x00000061u,7}, {0x00000062u,7}, {0x00000063u,7}, {0x00000064u,7}, {0x00000065u,7},
    {0x00000066u,7}, {0x00000067u,7}, {0x00000068u,7}, {0x00000069u,7}, {0x0000006au,7},
    {0x0000006bu,7}, {0x0000006cu,7}, {0x0000006du,7}, {0x0000006eu,7}, {0x0000006fu,7},
    {0x00000070u,7}, {0x00000071u,7}, {0x00000072u,7}, {0x000000fcu,8}, {0x00000073u,7},
    {0x000000fdu,8}, {0x00001ffbu,13}, {0x0007fff0u,19}, {0x00001ffcu,13}, {0x00003ffcu,14},
    {0x00000022u,6}, {0x00007ffdu,15}, {0x00000003u,5}, {0x00000023u,6}, {0x00000004u,5},
    {0x00000024u,6}, {0x00000005u,5}, {0x00000025u,6}, {0x00000026u,6}, {0x00000027u,6},
    {0x00000006u,5}, {0x00000074u,7}, {0x00000075u,7}, {0x00000028u,6}, {0x00000029u,6},
    {0x0000002au,6}, {0x00000007u,5}, {0x0000002bu,6}, {0x00000076u,7}, {0x0000002cu,6},
    {0x00000008u,5}, {0x00000009u,5}, {0x0000002du,6}, {0x00000077u,7}, {0x00000078u,7},
    {0x00000079u,7}, {0x0000007au,7}, {0x0000007bu,7}, {0x00007ffeu,15}, {0x000007fcu,11},
    {0x00003ffdu,14}, {0x00001ffdu,13}, {0x0ffffffcu,28}, {0x000fffe6u,20}, {0x003fffd2u,22},
    {0x000fffe7u,20}, {0x000fffe8u,20}, {0x003fffd3u,22}, {0x003fffd4u,22}, {0x003fffd5u,22},
    {0x007fffd9u,23}, {0x003fffd6u,22}, {0x007fffdau,23}, {0x007fffdbu,23}, {0x007fffdcu,23},
    {0x007fffddu,23}, {0x007fffdeu,23}, {0x00ffffebu,24}, {0x007fffdfu,23}, {0x00ffffecu,24},
    {0x00ffffedu,24}, {0x003fffd7u,22}, {0x007fffe0u,23}, {0x00ffffeeu,24}, {0x007fffe1u,23},
    {0x007fffe2u,23}, {0x007fffe3u,23}, {0x007fffe4u,23}, {0x001fffdcu,21}, {0x003fffd8u,22},
    {0x007fffe5u,23}, {0x003fffd9u,22}, {0x007fffe6u,23}, {0x007fffe7u,23}, {0x00ffffefu,24},
    {0x003fffdau,22}, {0x001fffddu,21}, {0x000fffe9u,20}, {0x003fffdbu,22}, {0x003fffdcu,22},
    {0x007fffe8u,23}, {0x007fffe9u,23}, {0x001fffdeu,21}, {0x007fffeau,23}, {0x003fffddu,22},
    {0x003fffdeu,22}, {0x00fffff0u,24}, {0x001fffdfu,21}, {0x003fffdfu,22}, {0x007fffebu,23},
    {0x007fffecu,23}, {0x001fffe0u,21}, {0x001fffe1u,21}, {0x003fffe0u,22}, {0x001fffe2u,21},
    {0x007fffedu,23}, {0x003fffe1u,22}, {0x007fffeeu,23}, {0x007fffefu,23}, {0x000fffeau,20},
    {0x003fffe2u,22}, {0x003fffe3u,22}, {0x003fffe4u,22}, {0x007ffff0u,23}, {0x003fffe5u,22},
    {0x003fffe6u,22}, {0x007ffff1u,23}, {0x03ffffe0u,26}, {0x03ffffe1u,26}, {0x000fffebu,20},
    {0x0007fff1u,19}, {0x003fffe7u,22}, {0x007ffff2u,23}, {0x003fffe8u,22}, {0x01ffffecu,25},
    {0x03ffffe2u,26}, {0x03ffffe3u,26}, {0x03ffffe4u,26}, {0x07ffffdeu,27}, {0x07ffffdfu,27},
    {0x03ffffe5u,26}, {0x00fffff1u,24}, {0x01ffffedu,25}, {0x0007fff2u,19}, {0x001fffe3u,21},
    {0x03ffffe6u,26}, {0x07ffffe0u,27}, {0x07ffffe1u,27}, {0x03ffffe7u,26}, {0x07ffffe2u,27},
    {0x00fffff2u,24}, {0x001fffe4u,21}, {0x001fffe5u,21}, {0x03ffffe8u,26}, {0x03ffffe9u,26},
    {0x0ffffffdu,28}, {0x07ffffe3u,27}, {0x07ffffe4u,27}, {0x07ffffe5u,27}, {0x000fffecu,20},
    {0x00fffff3u,24}, {0x000fffedu,20}, {0x001fffe6u,21}, {0x003fffe9u,22}, {0x001fffe7u,21},
    {0x001fffe8u,21}, {0x007ffff3u,23}, {0x003fffeau,22}, {0x003fffebu,22}, {0x01ffffeeu,25},
    {0x01ffffefu,25}, {0x00fffff4u,24}, {0x00fffff5u,24}, {0x03ffffeau,26}, {0x007ffff4u,23},
    {0x03ffffebu,26}, {0x07ffffe6u,27}, {0x03ffffecu,26}, {0x03ffffedu,26}, {0x07ffffe7u,27},
    {0x07ffffe8u,27}, {0x07ffffe9u,27}, {0x07ffffeau,27}, {0x07ffffebu,27}, {0x0ffffffeu,28},
    {0x07ffffecu,27}, {0x07ffffedu,27}, {0x07ffffeeu,27}, {0x07ffffefu,27}, {0x07fffff0u,27},
    {0x03ffffeeu,26}, {0x3fffffffu,30},
};

// Canonical-code decode trie. ch0/ch1 < 0 = no child; sym >= 0 = leaf.
struct HNode { int ch0, ch1, sym; };
static HNode kHuffTree[1024];
static int kHuffTreeCount = 0;
static bool kHuffBuilt = false;

static void huff_build() {
    kHuffTree[0].ch0 = kHuffTree[0].ch1 = -1; kHuffTree[0].sym = -1;
    kHuffTree[1].ch0 = kHuffTree[1].ch1 = -1; kHuffTree[1].sym = -1;   // root
    kHuffTreeCount = 2;
    for (int s = 0; s < 257 && kHuffTreeCount < 1024; s++) {
        uint32_t code = kHuffmanCodes[s].code;
        int bits = kHuffmanCodes[s].bits;
        int node = 1;
        for (int i = bits - 1; i >= 0; i--) {
            int bit = (int)((code >> i) & 1u);
            int* next = bit ? &kHuffTree[node].ch1 : &kHuffTree[node].ch0;
            if (*next <= 0) {
                if (kHuffTreeCount >= 1024) return;
                int n = kHuffTreeCount++;
                kHuffTree[n].ch0 = kHuffTree[n].ch1 = -1;
                kHuffTree[n].sym = -1;
                *next = n;
            }
            node = *next;
        }
        kHuffTree[node].sym = s;
    }
    kHuffBuilt = true;
}

// Decode a Huffman-encoded octet string into out (capacity outcap).
// Returns decoded length, or -1 on error. Trailing padding must be the
// EOS prefix (<=7 bits, all ones) per RFC 7541 5.2.
int huff_decode(const uint8_t* in, int inlen, uint8_t* out, int outcap) {
    if (!kHuffBuilt) huff_build();
    int outlen = 0;
    int bitpos = 0;
    int node = 1;
    int pending = 0;
    bool pending_all_ones = true;
    while (bitpos < inlen * 8) {
        int byte = in[bitpos >> 3];
        int bit = (byte >> (7 - (bitpos & 7))) & 1;
        bitpos++;
        int next = bit ? kHuffTree[node].ch1 : kHuffTree[node].ch0;
        if (next <= 0) return -1;                 // invalid code
        node = next;
        pending++;
        if (!bit) pending_all_ones = false;
        if (kHuffTree[node].sym >= 0) {
            int s = kHuffTree[node].sym;
            if (s == 256) return -1;              // EOS inside string
            if (outlen >= outcap) return -1;
            out[outlen++] = (uint8_t)s;
            node = 1;
            pending = 0;
            pending_all_ones = true;
        }
    }
    if (pending > 7 || !pending_all_ones) return -1;
    return outlen;
}

// ============================================================
// HPACK integer / string primitives
// ============================================================
// Decode an integer whose first byte is at b0 (n-bit prefix). Advances *out_end
// past the encoding. Returns the value, or -1 on error.
static int hpack_dec_int(const uint8_t* b0, const uint8_t* end, int nbits,
                         const uint8_t** out_end) {
    if (b0 >= end) return -1;
    int mask = (1 << nbits) - 1;
    int v = b0[0] & mask;
    const uint8_t* p = b0 + 1;
    if (v < mask) { *out_end = p; return v; }
    int m = 0;
    while (p < end) {
        uint8_t b = *p++;
        v += (int)(b & 0x7F) << m;
        m += 7;
        if (!(b & 0x80)) { *out_end = p; return v; }
        if (m > 28) return -1;
    }
    return -1;
}

// Decode a string literal at *pp (H flag byte + 7-bit length + data).
// Advances *pp past the literal. Returns decoded length into out (outcap), or -1.
static int hpack_dec_string(const uint8_t** pp, const uint8_t* end,
                            uint8_t* out, int outcap) {
    const uint8_t* p = *pp;
    if (p >= end) return -1;
    // the length byte IS the H flag + 7-bit prefix integer; hpack_dec_int reads
    // it as the integer's first byte and advances p past any continuation.
    const uint8_t* lenb = p;
    int len = hpack_dec_int(lenb, end, 7, &p);
    if (len < 0) return -1;
    bool huff = (lenb[0] & 0x80) != 0;
    if (p + len > end) return -1;
    const uint8_t* data = p;
    p += len;
    *pp = p;
    if (huff) return huff_decode(data, len, out, outcap);
    if (len > outcap) return -1;
    memcpy(out, data, (size_t)len);
    return len;
}

// ============================================================
// HPACK dynamic table (decoder side)
// ============================================================
struct DynEntry {
    char name[160];
    char value[384];
    int size;
};
static const int kDynMaxEntries = 64;
static const int kDynDefaultLimit = 4096;
static const int kDynHardLimit = 16384;   // defensive cap for size updates

struct DynTable {
    DynEntry entries[kDynMaxEntries];
    int count;
    int bytes;      // sum of entry sizes
    int limit;      // current maximum (SETTINGS_HEADER_TABLE_SIZE / update)
    int allowed;    // what we advertised (default 4096)

    DynTable() : count(0), bytes(0), limit(kDynDefaultLimit), allowed(kDynDefaultLimit) {}
};

static void dyn_resize(DynTable* d, int new_limit) {
    if (new_limit > d->allowed) new_limit = d->allowed;
    if (new_limit < 0) new_limit = 0;
    d->limit = new_limit;
    while (d->bytes > new_limit && d->count > 0) {
        d->count--;
        d->bytes -= d->entries[d->count].size;
    }
}

static void dyn_insert(DynTable* d, const char* name, int nlen, const char* value, int vlen) {
    if (nlen > 159 || vlen > 383) return;            // oversized: skip insert (still valid to ignore)
    int size = nlen + vlen + 32;
    if (size > d->limit) return;                     // too big to ever fit
    // make room
    while (d->bytes + size > d->limit && d->count > 0) {
        d->count--;
        d->bytes -= d->entries[d->count].size;
    }
    if (d->count >= kDynMaxEntries) {
        // evict oldest
        if (d->count == 0) return;
        d->count--;
        d->bytes -= d->entries[d->count].size;
        if (d->count >= kDynMaxEntries) return;
    }
    // shift right to insert at 0
    for (int i = d->count; i > 0; i--) d->entries[i] = d->entries[i - 1];
    memcpy(d->entries[0].name, name, (size_t)nlen);
    d->entries[0].name[nlen] = 0;
    memcpy(d->entries[0].value, value, (size_t)vlen);
    d->entries[0].value[vlen] = 0;
    d->entries[0].size = size;
    d->count++;
    d->bytes += size;
}

// Resolve an index: 1..61 static, 62.. dynamic. Returns false on OOB.
static bool table_get(const DynTable* d, int idx,
                      const char** name, int* nlen,
                      const char** value, int* vlen) {
    if (idx >= 1 && idx <= 61) {
        const StaticEntry& e = kStaticTable[idx - 1];
        *name = e.name; *nlen = (int)strlen(e.name);
        *value = e.value; *vlen = (int)strlen(e.value);
        return true;
    }
    int di = idx - 62;
    if (di >= 0 && di < d->count) {
        *name = d->entries[di].name;
        *nlen = (int)strlen(d->entries[di].name);
        *value = d->entries[di].value;
        *vlen = (int)strlen(d->entries[di].value);
        return true;
    }
    return false;
}

// ============================================================
// Header block decode -> Http2Response
// ============================================================
static void resp_add_header(Http2Response* r, const char* name, int nlen,
                            const char* value, int vlen) {
    if (nlen > 0 && name[0] == ':') {
        // pseudo-header
        if (nlen == 7 && memcmp(name, ":status", 7) == 0) {
            char tmp[16];
            int tl = vlen < 15 ? vlen : 15;
            memcpy(tmp, value, (size_t)tl);
            tmp[tl] = 0;
            r->status = atoi(tmp);
            r->status_text[0] = 0;
        }
        return;
    }
    if (r->header_count >= 32) return;
    char* line = r->headers[r->header_count];
    int pos = 0;
    if (nlen > 100) nlen = 100;
    if (vlen > 100) vlen = 100;
    memcpy(line + pos, name, (size_t)nlen); pos += nlen;
    line[pos++] = ':'; line[pos++] = ' ';
    memcpy(line + pos, value, (size_t)vlen); pos += vlen;
    line[pos] = 0;
    r->header_count++;
    // track content-length
    if (nlen == 14 && memcmp(name, "content-length", 14) == 0) {
        char tmp[24];
        int tl = vlen < 23 ? vlen : 23;
        memcpy(tmp, value, (size_t)tl);
        tmp[tl] = 0;
        r->content_length = atoi(tmp);
    }
}

// Decode one header block (already de-padded). Returns 0 ok / -1 error.

// diagnostics: enabled only for standalone builds (h2_diag); silent in the host
#ifdef NEFU_DIAG
#define HPK_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define HPK_LOG(...) ((void)0)
#endif

int hpack_decode_block(const uint8_t* data, int len, DynTable* dyn,
                              Http2Response* r) {
    const uint8_t* p = data;
    const uint8_t* end = data + len;
    while (p < end) {
        uint8_t first = *p;
        if (first & 0x80) {
            // indexed header field (RFC 7541 6.1: 7-bit prefix)
            int idx = hpack_dec_int(p, end, 7, &p);
            if (idx <= 0) { HPK_LOG("[hpack] idx<=0 at off %d, first=%02x, head:", (int)(p - data), first);
                int dn = len < 24 ? len : 24;
                for (int i = 0; i < dn; i++) HPK_LOG("%02x ", data[i]);
                HPK_LOG("\n"); return -1; }
            const char* n; int nl; const char* v; int vl;
            if (!table_get(dyn, idx, &n, &nl, &v, &vl)) { HPK_LOG("[hpack] lookup fail %d\n", idx); return -1; }
            resp_add_header(r, n, nl, v, vl);
        } else if ((first & 0xE0) == 0x20) {
            // dynamic table size update (5-bit prefix)
            int nsz = hpack_dec_int(p, end, 5, &p);
            if (nsz < 0 || nsz > kDynHardLimit) { HPK_LOG("[hpack] bad nsz %d\n", nsz); return -1; }
            dyn_resize(dyn, nsz);
        } else if ((first & 0xC0) == 0x40) {
            // literal with incremental indexing (6-bit prefix)
            int idx = hpack_dec_int(p, end, 6, &p);
            char nbuf[160]; int nlen = 0;
            const char* n; int nl;
            if (idx == 0) {
                nlen = hpack_dec_string(&p, end, (uint8_t*)nbuf, sizeof(nbuf) - 1);
                if (nlen < 0) { HPK_LOG("[hpack] name str fail at off %d\n", (int)(p - data)); return -1; }
                nbuf[nlen] = 0;
                n = nbuf; nl = nlen;
            } else {
                const char* vv; int vl;
                if (!table_get(dyn, idx, &n, &nl, &vv, &vl)) return -1;
            }
            char vbuf[384]; int vlen = hpack_dec_string(&p, end, (uint8_t*)vbuf, sizeof(vbuf) - 1);
            if (vlen < 0) { HPK_LOG("[hpack] value str fail at off %d\n", (int)(p - data)); return -1; }
            vbuf[vlen] = 0;
            resp_add_header(r, n, nl, vbuf, vlen);
            dyn_insert(dyn, n, nl, vbuf, vlen);
        } else {
            // literal without indexing (0x00) / never indexed (0x10): 4-bit prefix
            int idx = hpack_dec_int(p, end, 4, &p);
            char nbuf[160]; int nlen = 0;
            const char* n; int nl;
            if (idx == 0) {
                nlen = hpack_dec_string(&p, end, (uint8_t*)nbuf, sizeof(nbuf) - 1);
                if (nlen < 0) return -1;
                nbuf[nlen] = 0;
                n = nbuf; nl = nlen;
            } else {
                const char* vv; int vl;
                if (!table_get(dyn, idx, &n, &nl, &vv, &vl)) return -1;
            }
            char vbuf[384]; int vlen = hpack_dec_string(&p, end, (uint8_t*)vbuf, sizeof(vbuf) - 1);
            if (vlen < 0) return -1;
            vbuf[vlen] = 0;
            resp_add_header(r, n, nl, vbuf, vlen);
        }
    }
    return 0;
}

// ============================================================
// Request-side HPACK encoding (static table + literal, no Huffman)
// ============================================================
struct ReqBuf { uint8_t* d; int len; int cap; };

static bool rb_grow(ReqBuf* b, int need) {
    if (b->len + need <= b->cap) return true;
    int nc = b->cap ? b->cap : 256;
    while (nc < b->len + need) nc *= 2;
    uint8_t* nd = (uint8_t*)kalloc((size_t)nc);
    if (!nd) return false;
    if (b->d && b->len > 0) memcpy(nd, b->d, (size_t)b->len);
    if (b->d) kfree(b->d);
    b->d = nd;
    b->cap = nc;
    return true;
}
static bool rb_u8(ReqBuf* b, uint8_t v) {
    if (!rb_grow(b, 1)) return false;
    b->d[b->len++] = v;
    return true;
}
static bool rb_bytes(ReqBuf* b, const void* data, int n) {
    if (!rb_grow(b, n)) return false;
    memcpy(b->d + b->len, data, (size_t)n);
    b->len += n;
    return true;
}
// integer with N-bit prefix; base carries the representation's fixed bits
static bool rb_int(ReqBuf* b, int value, int nbits, uint8_t base) {
    int mask = (1 << nbits) - 1;
    if (value < mask) return rb_u8(b, (uint8_t)(base | value));
    if (!rb_u8(b, (uint8_t)(base | mask))) return false;
    value -= mask;
    while (value >= 128) {
        if (!rb_u8(b, (uint8_t)((value & 0x7F) | 0x80))) return false;
        value >>= 7;
    }
    return rb_u8(b, (uint8_t)value);
}
// string literal, H=0, 7-bit length prefix
static bool rb_str(ReqBuf* b, const char* s, int slen) {
    if (!rb_int(b, slen, 7, 0)) return false;
    return rb_bytes(b, s, slen);
}
// literal without indexing, name by static-table index
static bool rb_header_no_index(ReqBuf* b, int name_idx, const char* val, int vlen) {
    if (name_idx < 15) {
        if (!rb_u8(b, (uint8_t)name_idx)) return false;
    } else {
        if (!rb_u8(b, 0x0F)) return false;
        if (!rb_int(b, name_idx - 15, 7, 0)) return false;
    }
    return rb_str(b, val, vlen);
}

// ============================================================
// Frame layer
// ============================================================
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static bool send_frame(h2_send_fn sf, void* ctx, int type, int flags, int sid,
                       const uint8_t* payload, int plen) {
    uint8_t hdr[9];
    put24(hdr, plen);
    hdr[3] = (uint8_t)type;
    hdr[4] = (uint8_t)flags;
    hdr[5] = 0;                                     // R bit + stream id top bits
    hdr[6] = (uint8_t)(sid >> 16);
    hdr[7] = (uint8_t)(sid >> 8);
    hdr[8] = (uint8_t)sid;
    if (sf(ctx, hdr, 9) < 0) return false;
    if (plen > 0 && sf(ctx, payload, plen) < 0) return false;
    return true;
}

struct FrReader {
    h2_recv_fn rf;
    void* ctx;
    uint8_t buf[16384];
    int len;
    int pos;
};

static int fr_fill(FrReader* fr, int need) {
    while (fr->len - fr->pos < need) {
        if (fr->len - fr->pos > 0) {
            memmove(fr->buf, fr->buf + fr->pos, (size_t)(fr->len - fr->pos));
            fr->len -= fr->pos;
            fr->pos = 0;
        } else {
            fr->len = 0;
            fr->pos = 0;
        }
        int r = fr->rf(fr->ctx, fr->buf + fr->len, (int)sizeof(fr->buf) - fr->len, 8000);
        if (r <= 0) return -1;
        fr->len += r;
    }
    return 0;
}

// Read one frame. Returns the frame type (>=0), or -1 on error/timeout.
// *payload (freshly kalloc'd, caller frees) holds the frame body; *plen its length.
static int fr_frame(FrReader* fr, int* flags, int* sid, uint8_t** payload, int* plen) {
    *payload = 0; *plen = 0;
    if (fr_fill(fr, 9) < 0) return -1;
    const uint8_t* h = fr->buf + fr->pos;
    int len = ((int)h[0] << 16) | ((int)h[1] << 8) | h[2];
    int type = h[3];
    *flags = h[4];
    *sid = (((int)h[5] & 0x7F) << 24) | ((int)h[6] << 16) | ((int)h[7] << 8) | h[8];
    fr->pos += 9;
    if (len > (1 << 20)) return -1;              // defensive: no frame > 1 MiB
    if (len == 0) return type;
    if (fr_fill(fr, len) < 0) return -1;
    uint8_t* pb = (uint8_t*)kalloc((size_t)len);
    if (!pb) return -1;
    memcpy(pb, fr->buf + fr->pos, (size_t)len);
    fr->pos += len;
    *payload = pb;
    *plen = len;
    return type;
}

// growing body / header-block accumulator
struct GrowBuf { uint8_t* d; int len; int cap; };
static bool gb_grow(GrowBuf* b, int need) {
    if (b->len + need <= b->cap) return true;
    int nc = b->cap ? b->cap : 4096;
    while (nc < b->len + need) nc *= 2;
    uint8_t* nd = (uint8_t*)kalloc((size_t)nc);
    if (!nd) return false;
    if (b->d && b->len > 0) memcpy(nd, b->d, (size_t)b->len);
    if (b->d) kfree(b->d);
    b->d = nd;
    b->cap = nc;
    return true;
}
static bool gb_append(GrowBuf* b, const uint8_t* data, int n) {
    if (!gb_grow(b, n)) return false;
    memcpy(b->d + b->len, data, (size_t)n);
    b->len += n;
    return true;
}

// ============================================================
// HTTP/2 GET (one stream)
// ============================================================
// Diagnostics hook: decode an HPACK header block into a fresh response.
int http2_test_decode(const uint8_t* data, int len, Http2Response* r) {
    DynTable dyn;
    return hpack_decode_block(data, len, &dyn, r);
}

static const int kConnWindow = 0x00400000;      // 4 MiB advertised
static const int kMaxBody = 64 * 1024 * 1024;   // defensive body cap
static const int kMaxHeaderBlock = 128 * 1024;  // defensive header block cap

static void set_err(char* err, int errsz, const char* msg) {
    if (err && errsz > 0) {
        snprintf(err, (size_t)errsz, "%s", msg);
    }
}

Http2Response* http2_get(h2_recv_fn rf, h2_send_fn sf, void* ctx,
                         const char* authority, const char* path,
                         char* err, int errsz) {
    if (err && errsz > 0) err[0] = 0;
    if (!rf || !sf || !authority || !*authority || !path) {
        set_err(err, errsz, "bad args");
        return 0;
    }

    // ---- HPACK-encode the request header block ----
    ReqBuf hb; memset(&hb, 0, sizeof(hb));
    bool ok = rb_u8(&hb, 0x82);                                   // :method GET (index 2)
    ok = ok && rb_u8(&hb, 0x87);                                  // :scheme https (index 7)
    ok = ok && rb_header_no_index(&hb, 1, authority, (int)strlen(authority));  // :authority
    ok = ok && rb_header_no_index(&hb, 4, path, (int)strlen(path));            // :path
    static const char kUA[] =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";
    ok = ok && rb_header_no_index(&hb, 58, kUA, (int)strlen(kUA));            // user-agent
    static const char kAccept[] =
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
    ok = ok && rb_header_no_index(&hb, 19, kAccept, (int)strlen(kAccept));    // accept
    if (!ok) {
        if (hb.d) kfree(hb.d);
        set_err(err, errsz, "hpack encode");
        return 0;
    }

    // ---- connection preface + SETTINGS + WINDOW_UPDATE + HEADERS ----
    static const char kPreface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (sf(ctx, (const uint8_t*)kPreface, 24) < 0) {
        kfree(hb.d);
        set_err(err, errsz, "send preface");
        return 0;
    }
    uint8_t settings[12];
    put16(settings, 0x0002); put32(settings + 2, 0);                  // ENABLE_PUSH = 0
    put16(settings + 6, 0x0004); put32(settings + 8, (uint32_t)kConnWindow);  // INITIAL_WINDOW_SIZE
    if (!send_frame(sf, ctx, 0x4, 0, 0, settings, 12)) {
        kfree(hb.d);
        set_err(err, errsz, "send settings");
        return 0;
    }
    uint8_t winc[4];
    put32(winc, (uint32_t)(kConnWindow - 65535));                    // bump conn window
    if (!send_frame(sf, ctx, 0x8, 0, 0, winc, 4) ||
        !send_frame(sf, ctx, 0x1, 0x5, 1, hb.d, hb.len)) {           // HEADERS, END_HEADERS|END_STREAM
        kfree(hb.d);
        set_err(err, errsz, "send headers");
        return 0;
    }
    kfree(hb.d);

    // ---- response loop ----
    FrReader fr; memset(&fr, 0, sizeof(fr));
    fr.rf = rf; fr.ctx = ctx;
    DynTable dyn;
    Http2Response* resp = new Http2Response();
    GrowBuf hblock; memset(&hblock, 0, sizeof(hblock));   // header block accumulator
    GrowBuf body;   memset(&body, 0, sizeof(body));
    uint32_t conn_data = 0;
    uint32_t stream_data = 0;
    bool awaiting_headers = true;
    bool stream_done = false;

    for (;;) {
        int flags = 0, sid = 0, plen = 0;
        uint8_t* payload = 0;
        int type = fr_frame(&fr, &flags, &sid, &payload, &plen);
        if (type < 0) { set_err(err, errsz, "recv frame"); goto fail; }

        switch (type) {
        case 0x0: {  // DATA
            const uint8_t* d = payload;
            int dl = plen;
            if (flags & 0x8) {  // PADDED
                if (plen < 1) { kfree(payload); set_err(err, errsz, "bad data"); goto fail; }
                int pad = payload[0];
                if (1 + pad > plen) { kfree(payload); set_err(err, errsz, "bad data"); goto fail; }
                d = payload + 1;
                dl = plen - 1 - pad;
            }
            if (sid == 1 && dl > 0) {
                if (body.len + dl > kMaxBody) { kfree(payload); set_err(err, errsz, "body too big"); goto fail; }
                if (!gb_append(&body, d, dl)) { kfree(payload); set_err(err, errsz, "oom"); goto fail; }
            }
            if (sid == 1) {
                conn_data += (uint32_t)plen;
                stream_data += (uint32_t)dl;
                if (conn_data >= (uint32_t)kConnWindow / 2) {
                    uint8_t w[4]; put32(w, conn_data);
                    send_frame(sf, ctx, 0x8, 0, 0, w, 4);
                    conn_data = 0;
                }
                if (stream_data >= (uint32_t)kConnWindow / 2) {
                    uint8_t w[4]; put32(w, stream_data);
                    send_frame(sf, ctx, 0x8, 0, 1, w, 4);
                    stream_data = 0;
                }
                if (flags & 0x1) stream_done = true;
            }
            kfree(payload);
            if (stream_done) goto done;
            break;
        }
        case 0x1: {  // HEADERS
            if (!awaiting_headers) { kfree(payload); set_err(err, errsz, "extra headers"); goto fail; }
            const uint8_t* hd = payload;
            int hdl = plen;
            if (flags & 0x8) {  // PADDED
                if (hdl < 1) { kfree(payload); set_err(err, errsz, "bad headers"); goto fail; }
                int pad = hd[0];
                if (1 + pad > hdl) { kfree(payload); set_err(err, errsz, "bad headers"); goto fail; }
                hd += 1; hdl = hdl - 1 - pad;
            }
            if (flags & 0x20) {  // PRIORITY
                if (hdl < 5) { kfree(payload); set_err(err, errsz, "bad headers"); goto fail; }
                hd += 5; hdl -= 5;
            }
            if (hblock.len + hdl > kMaxHeaderBlock) { kfree(payload); set_err(err, errsz, "headers too big"); goto fail; }
            if (!gb_append(&hblock, hd, hdl)) { kfree(payload); set_err(err, errsz, "oom"); goto fail; }
            kfree(payload);
            if (flags & 0x4) {  // END_HEADERS
                if (hpack_decode_block(hblock.d, hblock.len, &dyn, resp) != 0) {
                    set_err(err, errsz, "hpack decode");
                    goto fail;
                }
                hblock.len = 0;
                awaiting_headers = false;
                if (flags & 0x1) stream_done = true;
            }
            if (stream_done) goto done;
            break;
        }
        case 0x9:  // CONTINUATION (only while a HEADERS block is pending)
            if (hblock.len + plen > kMaxHeaderBlock) { kfree(payload); set_err(err, errsz, "headers too big"); goto fail; }
            if (!gb_append(&hblock, payload, plen)) { kfree(payload); set_err(err, errsz, "oom"); goto fail; }
            kfree(payload);
            if (flags & 0x4) {  // END_HEADERS
                if (hpack_decode_block(hblock.d, hblock.len, &dyn, resp) != 0) {
                    set_err(err, errsz, "hpack decode");
                    goto fail;
                }
                hblock.len = 0;
                awaiting_headers = false;
                if (flags & 0x1) stream_done = true;
            }
            if (stream_done) goto done;
            break;
        case 0x4:  // SETTINGS
            if (!(flags & 0x1)) send_frame(sf, ctx, 0x4, 0x1, 0, 0, 0);  // ACK
            kfree(payload);
            break;
        case 0x6:  // PING
            if (!(flags & 0x1)) send_frame(sf, ctx, 0x6, 0x1, 0, payload, plen);  // ACK
            kfree(payload);
            break;
        case 0x8:  // WINDOW_UPDATE (we never depend on it)
            kfree(payload);
            break;
        case 0x3:  // RST_STREAM
            if (sid == 1) { kfree(payload); set_err(err, errsz, "stream reset"); goto fail; }
            kfree(payload);
            break;
        case 0x7:  // GOAWAY
            kfree(payload);
            set_err(err, errsz, "goaway");
            goto fail;
        default:   // PUSH_PROMISE / PRIORITY / unknown: ignore
            kfree(payload);
            break;
        }
    }

done:
    if (hblock.d) kfree(hblock.d);
    resp->body = body.d;
    resp->body_len = body.len;
    resp->end_stream_seen = stream_done;
    if (resp->status == 0) {
        // no :status: treat as failure
        set_err(err, errsz, "no status");
        if (resp->body) kfree(resp->body);
        delete resp;
        return 0;
    }
    return resp;

fail:
    if (hblock.d) kfree(hblock.d);
    if (body.d) kfree(body.d);
    delete resp;
    return 0;
}

} // namespace http2
} // namespace nefu
