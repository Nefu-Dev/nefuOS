// nefuOS DEFLATE / zlib library — compress & decompress
// Self-contained RFC 1950 (zlib) + RFC 1951 (DEFLATE).
// Decompress supports stored/fixed/dynamic blocks;
// compress uses LZ77 (hash chains) + fixed-Huffman blocks, chunked at 32 KiB.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace deflate {

// zlib compress. Returns compressed size, or -1 if dst too small.
int zlib_compress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap);

// zlib decompress. Returns decompressed size, or -1 on error / small buffer.
int zlib_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap);

// raw deflate decompress (no zlib header/trailer)
int raw_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap);

// raw deflate compress (no zlib header/trailer)
int raw_compress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap);

} // namespace deflate
} // namespace nefu
