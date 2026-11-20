/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// LVGL 9 glyph source for the built-in CJK bitmap font.
//
// The desktop is bilingual (T("中文","English") in the UI code), but LVGL's
// bundled Montserrat fonts only carry Latin glyphs: as soon as g_settings.lang
// switched to Chinese every label turned into tofu boxes and, because the
// placeholder cell is wider than the icon card, the text appeared to vanish.
//
// This file exposes lv_font_cjk_16: a 16px font whose CJK glyphs come from the
// complete GB2312 table in core/apps/cjk_font.h (7445 code points, 16x16 1bpp)
// and whose ASCII glyphs are delegated to Montserrat through the LVGL fallback
// chain, so English keeps the same look it had before.
//
// Metrics are deliberately identical to lv_font_montserrat_16 so Chinese and
// Latin share one baseline inside the same line:
//   line_height = 18, base_line = 3, box 16x16, ofs_y = 0  ->  the cell fills
//   y = pos-1 .. pos+14 with the baseline at pos+15, i.e. CJK and Latin share a line.
#include "lvgl.h"
#include "lv_cjk_font.h"
#include "../apps/cjk_font.h"   // g_cjk_count / g_cjk_uni[] / g_cjk_bits[]

LV_FONT_DECLARE(lv_font_montserrat_16);

// optional diagnostics: build with -DNEFU_CJK_TRACE to log unresolved code
// points (symbols like U+F00D come from the Montserrat fallback on purpose).
#ifdef NEFU_CJK_TRACE
#include <cstdio>
#define CJK_TRACE(...) fprintf(stderr, "[cjk] " __VA_ARGS__)
#else
#define CJK_TRACE(...) do {} while (0)
#endif

namespace nefu {

// glyph cell geometry (see the comment block above)
static const int CJK_CELL = 16;
static const int CJK_OFS_Y = 0;      // see letter_coords.y1 in lv_draw_label.c:405

// g_cjk_uni is ascending, so a plain binary search is enough.
static int cjk_index(uint32_t uc) {
    int lo = 0, hi = g_cjk_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t v = (uint32_t)g_cjk_uni[mid];
        if (v == uc) return mid;
        if (v < uc) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

static bool lv_cjk_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                 uint32_t letter, uint32_t letter_next) {
    (void)letter_next;
    // ASCII and control codes: let the Montserrat fallback render them.
    if (letter < 0x80) return false;
    int idx = cjk_index(letter);
    if (idx < 0) return false;          // unknown -> Montserrat fallback / placeholder

    dsc_out->box_w = (uint16_t)CJK_CELL;
    dsc_out->box_h = (uint16_t)CJK_CELL;
    dsc_out->adv_w = (uint16_t)CJK_CELL;
    dsc_out->ofs_x = 0;
    dsc_out->ofs_y = (int16_t)CJK_OFS_Y;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A1;
    dsc_out->is_placeholder = 0;
    dsc_out->gid.index = (uint32_t)idx;
    dsc_out->resolved_font = font;
    return true;
}

// LVGL 9 bitmap-font contract (src/draw/sw/lv_draw_sw_letter.c:94): the value
// returned here is cast back to lv_draw_buf_t*, and the mask is read from
// draw_buf->data with draw_buf->header.stride. So we must unpack the A1 rows
// into the caller's A8 scratch buffer and hand back the buffer itself - NOT a
// pointer into the packed rodata (doing that used to dereference a fake header
// and fault the process).
static const void* lv_cjk_get_glyph_bitmap(lv_font_glyph_dsc_t* dsc, lv_draw_buf_t* draw_buf) {
    uint32_t idx = dsc->gid.index;
    if (draw_buf == NULL || (int)idx >= g_cjk_count) return NULL;
    uint8_t* out = draw_buf->data;
    uint32_t stride = draw_buf->header.stride;
    const uint8_t* src = &g_cjk_bits[(size_t)idx * 32u];   // 16 rows x 2 bytes, MSB left
    for (int y = 0; y < CJK_CELL; y++) {
        const uint8_t* row = src + (size_t)y * 2u;
        uint8_t* dst = out + (size_t)y * stride;
        for (int x = 0; x < CJK_CELL; x++)
            dst[x] = (row[x >> 3] & (uint8_t)(0x80u >> (x & 7))) ? 0xFF : 0x00;
    }
    return draw_buf;
}

static void lv_cjk_release_glyph(const lv_font_t* font, lv_font_glyph_dsc_t* dsc) {
    (void)font; (void)dsc;   // static rodata: nothing to release
}

lv_font_t lv_font_cjk_16 = {
    .get_glyph_dsc = lv_cjk_get_glyph_dsc,
    .get_glyph_bitmap = lv_cjk_get_glyph_bitmap,
    .release_glyph = lv_cjk_release_glyph,
    .line_height = 18,          // same as montserrat_16
    .base_line = 3,             // same as montserrat_16
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = LV_FONT_KERNING_NORMAL,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = NULL,
    .fallback = &lv_font_montserrat_16,   // Latin + missing code points
    .user_data = NULL,
};

// Kept for callers that used to inject the fallback at runtime.
void lv_cjk_font_init(lv_font_t* fallback) {
    if (fallback) lv_font_cjk_16.fallback = fallback;
}

} // namespace nefu
