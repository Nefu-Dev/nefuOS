// nefuOS minimal baseline-JPEG decoder (bare-metal safe: integer only).
// Supports SOF0 (baseline), 8-bit, YCbCr 4:4:4 / 4:2:2 / 4:2:0 and grayscale,
// with optional restart markers. Everything runs with fixed-point math so the
// bare kernel links without a floating-point library.
#pragma once
#include <stdint.h>

namespace nefu {

struct Surface;

// Decodes a JPEG file into a 32bpp surface. Returns true on success.
bool jpeg_decode(const uint8_t* data, uint32_t size, Surface& out);

} // namespace nefu
