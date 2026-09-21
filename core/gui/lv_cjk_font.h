// LVGL-compatible CJK font wrapper header.
#ifndef NEFU_LV_CJK_FONT_H
#define NEFU_LV_CJK_FONT_H

#include "lvgl.h"

namespace nefu {

// A font that supports both ASCII (8x16) and CJK (16x16) bitmap glyphs.
// Use this instead of lv_font_montserrat_16 anywhere Chinese text is needed.
extern lv_font_t lv_font_cjk_16;

// Initialize the CJK font with a fallback for Latin characters.
// Usually you pass &lv_font_montserrat_16 as the fallback.
void lv_cjk_font_init(lv_font_t* fallback);

} // namespace nefu

#endif // NEFU_LV_CJK_FONT_H
