// nefuOS TrueType font renderer (stb_truetype, MIT).
// Renders scalable vector text from an embedded open font (VT323, OFL-1.1).
// Falls back cleanly to the built-in bitmap font when disabled.
#pragma once

namespace nefu {

// Initialize the TTF engine from the embedded font data. Idempotent.
// Returns true when scalable text is available.
bool ttf_init();

bool ttf_ready();

// Rendered advance width of a UTF-8 string at the given pixel height.
int ttf_text_width(const char* s, int size);

// Draw UTF-8 text at (x, y) with the given pixel height. The y coordinate
// is the top of the first line. Multi-line ("\n") is supported.
void ttf_draw_text(void* fb_surface, int x, int y, const char* s, int size,
                   unsigned int fg, unsigned int bg);

// Draw text and auto-wrap at max_width (used by the Font Viewer).
void ttf_draw_wrapped(void* fb_surface, int x, int y, const char* s, int size,
                      unsigned int fg, unsigned int bg, int max_width, int* out_y);

} // namespace nefu
