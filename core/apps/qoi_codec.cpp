/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// QOI decoder - see qoi_codec.h for the contract.
#include "qoi_codec.h"
#include <cstring>

namespace nefu {

namespace {

// the QOI header stores width/height big-endian
uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// 0x00000000FFFFFFFF'ish hash of the spec: index = (r*3 + g*5 + b*7 + a*11) % 64
inline uint8_t hash_of(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint8_t)(((uint32_t)r * 3u + (uint32_t)g * 5u + (uint32_t)b * 7u + (uint32_t)a * 11u) & 63u);
}

} // namespace

uint32_t qoi_rgba_size(const uint8_t* in, uint32_t len) {
    if (!in || len < 14) return 0;
    if (memcmp(in, "qoif", 4) != 0) return 0;
    uint32_t w = rd32(in + 4), h = rd32(in + 8);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return 0;
    return w * h * 4u;
}

bool qoi_decode(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t out_cap) {
    if (!in || !out) return false;
    uint32_t need = qoi_rgba_size(in, len);
    if (need == 0 || need > out_cap) return false;
    uint32_t npx = need / 4u;

    uint8_t index[64][4];
    memset(index, 0, sizeof(index));
    uint8_t r = 0, g = 0, b = 0, a = 255;
    uint32_t p = 14;
    uint32_t i = 0;
    while (i < npx) {
        if (p >= len) return false;
        uint8_t op = in[p++];
        if (op == 0xFE) {                    // QOI_OP_RGB
            if (p + 3 > len) return false;
            r = in[p]; g = in[p + 1]; b = in[p + 2];
            p += 3;
        } else if (op == 0xFF) {             // QOI_OP_RGBA
            if (p + 4 > len) return false;
            r = in[p]; g = in[p + 1]; b = in[p + 2]; a = in[p + 3];
            p += 4;
        } else {
            switch (op & 0xC0) {
            case 0x00: {                     // QOI_OP_INDEX
                uint8_t k = (uint8_t)(op & 0x3F);
                r = index[k][0]; g = index[k][1]; b = index[k][2]; a = index[k][3];
                break;
            }
            case 0x40: {                     // QOI_OP_DIFF
                r = (uint8_t)(r + ((op >> 4) & 0x03) - 2);
                g = (uint8_t)(g + ((op >> 2) & 0x03) - 2);
                b = (uint8_t)(b + (op & 0x03) - 2);
                break;
            }
            case 0x80: {                     // QOI_OP_LUMA (+1 byte)
                if (p >= len) return false;
                uint8_t b2 = in[p++];
                uint8_t dg = (uint8_t)((op & 0x3F) - 32);
                g = (uint8_t)(g + dg);
                r = (uint8_t)(r + dg + ((b2 >> 4) & 0x0F) - 8);
                b = (uint8_t)(b + dg + (b2 & 0x0F) - 8);
                break;
            }
            default: {                       // QOI_OP_RUN
                uint32_t run = (uint32_t)(op & 0x3F) + 1u;
                if (i + run > npx) return false;
                while (run--) {
                    out[i * 4 + 0] = r; out[i * 4 + 1] = g;
                    out[i * 4 + 2] = b; out[i * 4 + 3] = a;
                    i++;
                }
                continue;                    // colour unchanged, skip the store below
            }
            }
        }
        index[hash_of(r, g, b, a)][0] = r;
        index[hash_of(r, g, b, a)][1] = g;
        index[hash_of(r, g, b, a)][2] = b;
        index[hash_of(r, g, b, a)][3] = a;
        out[i * 4 + 0] = r; out[i * 4 + 1] = g;
        out[i * 4 + 2] = b; out[i * 4 + 3] = a;
        i++;
    }
    return true;
}

} // namespace nefu
