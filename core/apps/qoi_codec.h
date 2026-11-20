/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// QOI (Quite OK Image) decoder.
//
// The bare build has no host codec library, so the built-in demo video stores
// its frames as QOI blobs (see tools/gen_nvid_demo.py). QOI is lossless,
// decodes in a few dozen instructions per pixel and needs no allocator, which
// keeps the bare video path independent from any third-party decoder.
//
// Wire format (https://qoiformat.org/qoi-specification.pdf):
//   "qoif" | u32 width | u32 height | u8 channels | u8 colorspace | chunks | 7x00 01
// Output is always RGBA, 4 bytes per pixel, top-down.
#pragma once
#include <stdint.h>

namespace nefu {

// Size of the RGBA buffer a decoder call needs for a given header.
// Returns 0 when the header is not a valid QOI header.
uint32_t qoi_rgba_size(const uint8_t* in, uint32_t len);

// Decode a QOI blob into `out` (w*h*4 bytes, RGBA8888).
// `out_cap` must be >= qoi_rgba_size(). Returns false on malformed input.
bool qoi_decode(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t out_cap);

} // namespace nefu
