// LVGL-compatible CJK font wrapper.
// Wraps our built-in 8x16 ASCII + 16x16 CJK bitmap fonts into lv_font_t format
// so LVGL labels can render Chinese characters without showing tofu boxes.
#include "lvgl.h"
// External declarations (avoid multiple definition from font.h)
namespace nefu {
extern const unsigned char font8x16[96][16];
}
#include "../gui/gfx.h"

namespace nefu {

// Forward declarations from gfx.cpp
uint32_t utf8_next(const char*& p);
const uint8_t* glyph16(uint32_t uc);

static bool lv_cjk_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                  uint32_t letter, uint32_t letter_next) {
    (void)font; (void)letter_next;
    memset(dsc_out, 0, sizeof(*dsc_out));

    if (letter < 0x80) {
        // ASCII: 8x16
        dsc_out->box_w = 8;
        dsc_out->box_h = 16;
        dsc_out->adv_w = 8;
        dsc_out->ofs_x = 0;
        dsc_out->ofs_y = 0;
        dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
        dsc_out->gid.index = letter;
    } else {
        // CJK: 16x16
        const uint8_t* d = gfx::glyph16(letter);
        if (!d) {
            // Not found: return placeholder
            dsc_out->box_w = 16;
            dsc_out->box_h = 16;
            dsc_out->adv_w = 16;
            dsc_out->is_placeholder = 1;
            return true;
        }
        dsc_out->box_w = 16;
        dsc_out->box_h = 16;
        dsc_out->adv_w = 16;
        dsc_out->ofs_x = 0;
        dsc_out->ofs_y = 0;
        dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
        dsc_out->gid.index = letter;
    }
    dsc_out->resolved_font = font;
    return true;
}

static const void* lv_cjk_get_glyph_bitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* draw_buf) {
    (void)draw_buf;
    // For simplicity, we render on-the-fly. But LVGL expects a bitmap pointer.
    // This is a simplified approach: we use a static buffer (not thread-safe but OK for single-core).
    static uint8_t s_bitmap[16 * 16]; // max glyph size

    uint32_t letter = dsc->gid.index;
    memset(s_bitmap, 0, sizeof(s_bitmap));

    if (letter < 0x80) {
        // ASCII 8x16
        int idx = ((unsigned char)letter >= 32 && (unsigned char)letter <= 127)
                  ? (int)letter - 32 : 0;
        for (int row = 0; row < 16; row++) {
            unsigned char b = nefu::font8x16[idx][row];
            for (int col = 0; col < 8; col++) {
                if (b & (0x80 >> col)) {
                    s_bitmap[row * 8 + col] = 0xFF;
                }
            }
        }
    } else {
        // CJK 16x16
        const uint8_t* d = gfx::glyph16(letter);
        if (d) {
            for (int row = 0; row < 16; row++) {
                unsigned short b = (unsigned short)((d[row * 2] << 8) | d[row * 2 + 1]);
                for (int col = 0; col < 16; col++) {
                    if (b & (0x8000 >> col)) {
                        s_bitmap[row * 16 + col] = 0xFF;
                    }
                }
            }
        }
    }
    return s_bitmap;
}

static void lv_cjk_release_glyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc) {
    (void)font; (void)dsc;
    // Nothing to release (static buffer)
}

// The CJK-enabled font object
lv_font_t lv_font_cjk_16 = {
    .get_glyph_dsc = lv_cjk_get_glyph_dsc,
    .get_glyph_bitmap = lv_cjk_get_glyph_bitmap,
    .release_glyph = lv_cjk_release_glyph,
    .line_height = 16,
    .base_line = 16,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = 0,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = NULL,
    .fallback = NULL, // Will be set to Montserrat for Latin chars
    .user_data = NULL,
};

// Initialize: set fallback to Montserrat
void lv_cjk_font_init(lv_font_t* fallback) {
    lv_font_cjk_16.fallback = fallback;
}

} // namespace nefu
