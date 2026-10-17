// nefuOS minimal ZIP reader for .nefud Linux .deb payloads.
// Implementation (see zip.h). Pure C++, depends only on deflate.h.
#include "zip.h"
#include "deflate.h"

namespace nefu {
namespace zip {

namespace {

bool find_eocd(const uint8_t* d, size_t n, uint32_t* cd_off, uint16_t* count) {
    if (n < 22) return false;
    size_t lo = 0;
    if (n > 22 + 65535) lo = n - (22 + 65535); // max comment length 65535
    for (long long i = (long long)(n - 22); i >= (long long)lo; i--) {
        const uint8_t* p = d + i;
        if (p[0] == 0x50 && p[1] == 0x4B && p[2] == 0x05 && p[3] == 0x06) {
            if (count) *count = (uint16_t)(p[10] | (p[11] << 8));
            if (cd_off) *cd_off = (uint32_t)p[16] | ((uint32_t)p[17] << 8) |
                                  ((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24);
            return true;
        }
    }
    return false;
}

uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool name_eq(const char* a, const char* b) {
    int i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return false;
    return a[i] == 0 && b[i] == 0;
}

// Shared fill for the idx-th central-directory entry.
int fill_entry(const uint8_t* d, size_t n, uint32_t cd_off, int idx, Entry* out) {
    size_t p = cd_off;
    for (int i = 0; i <= idx; i++) {
        if (p + 46 > n) return -1;
        if (!(d[p] == 0x50 && d[p + 1] == 0x4B && d[p + 2] == 0x01 && d[p + 3] == 0x02))
            return -1;
        uint16_t nl = le16(d + p + 28);
        uint16_t el = le16(d + p + 30);
        uint16_t cl = le16(d + p + 32);
        if (p + 46 + (size_t)nl > n) return -1;
        if (i == idx) {
            int nn = (int)nl;
            if (nn > (int)sizeof(out->name) - 1) nn = (int)sizeof(out->name) - 1;
            for (int k = 0; k < nn; k++) out->name[k] = (char)d[p + 46 + k];
            out->name[nn] = 0;
            out->method = le16(d + p + 10);        // central method at +10
            out->comp_size = le32(d + p + 20);
            out->uncomp_size = le32(d + p + 24);
            out->lho = le32(d + p + 42);
            out->name_len = 0;
            out->extra_len = 0;
            // local header at lho: name_len +26, extra_len +28
            if ((uint64_t)out->lho + 30 <= n &&
                d[out->lho] == 0x50 && d[out->lho + 1] == 0x4B &&
                d[out->lho + 2] == 0x03 && d[out->lho + 3] == 0x04) {
                out->name_len = le16(d + out->lho + 26);
                out->extra_len = le16(d + out->lho + 28);
            }
            return 1;
        }
        p += 46 + (size_t)nl + (size_t)el + (size_t)cl;
    }
    return 0;
}

} // namespace

int is_zip(const uint8_t* d, size_t n) {
    if (!d || n < 4) return 0;
    return (d[0] == 'P' && d[1] == 'K' && d[2] == 0x03 && d[3] == 0x04) ? 1 : 0;
}

int count(const uint8_t* d, size_t n) {
    uint16_t cnt;
    if (!d || !find_eocd(d, n, 0, &cnt)) return -1;
    return (int)cnt;
}

int entry(const uint8_t* d, size_t n, int idx, Entry* out) {
    uint32_t cd_off;
    uint16_t cnt;
    if (!d || !out || idx < 0) return -1;
    if (!find_eocd(d, n, &cd_off, &cnt) || idx >= (int)cnt) return 0;
    return fill_entry(d, n, cd_off, idx, out);
}

int find(const uint8_t* d, size_t n, const char* name, Entry* out) {
    uint32_t cd_off;
    uint16_t cnt;
    if (!d || !name || !out) return -1;
    if (!find_eocd(d, n, &cd_off, &cnt)) return -1;
    for (int i = 0; i < (int)cnt; i++) {
        if (fill_entry(d, n, cd_off, i, out) != 1) continue;
        if (name_eq(name, out->name)) return 1;
    }
    return 0;
}

long read(const uint8_t* d, size_t n, const Entry& e, uint8_t* dst, size_t cap) {
    if (!d || !dst) return -1;
    uint64_t data_off = (uint64_t)e.lho + 30u + e.name_len + e.extra_len;
    if (data_off + e.comp_size > n) return -1;
    if (e.method == 0) { // stored
        if (e.uncomp_size > cap) return -1;
        for (uint32_t i = 0; i < e.uncomp_size; i++) dst[i] = d[data_off + i];
        return (long)e.uncomp_size;
    }
    if (e.method == 8) { // deflate (raw RFC1951, no zlib header)
        return (long)deflate::raw_decompress(d + data_off, (size_t)e.comp_size, dst, cap);
    }
    return -1;
}

} // namespace zip
} // namespace nefu
