// nefuOS minimal ZIP reader for .nefud Linux .deb payloads.
// Reads via the central directory (EOCD + PK\x01\x02 entries). Supports only
// stored (method 0) and raw-DEFLATE (method 8) entries. No ZIP64, no
// encryption, no reliance on data descriptors (sizes come from the central
// directory, so flag bit 3 archives still read correctly).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace zip {

struct Entry {
    char name[256];       // NUL-terminated entry name
    uint32_t lho;         // local file header offset
    uint32_t comp_size;   // compressed size (central directory)
    uint32_t uncomp_size; // uncompressed size (central directory)
    uint16_t method;      // 0 = stored, 8 = deflate
    uint16_t name_len;    // local header name length (for data offset)
    uint16_t extra_len;   // local header extra length
};

// 1 if d begins with a ZIP local-file-header signature (PK\x03\x04), else 0.
int is_zip(const uint8_t* d, size_t n);

// Number of entries via the central directory, or -1 if not a ZIP.
int count(const uint8_t* d, size_t n);

// Read the idx-th central-directory entry into out.
// Returns 1 (ok), 0 (idx out of range), -1 (not a ZIP / structural error).
int entry(const uint8_t* d, size_t n, int idx, Entry* out);

// Find the first entry whose name matches exactly.
// Returns 1 (fills out), 0 (not found), -1 (not a ZIP / error).
int find(const uint8_t* d, size_t n, const char* name, Entry* out);

// Decompress one entry into dst (cap bytes). Returns bytes written, or -1.
long read(const uint8_t* d, size_t n, const Entry& e, uint8_t* dst, size_t cap);

} // namespace zip
} // namespace nefu
