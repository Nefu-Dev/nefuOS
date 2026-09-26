// nefuOS built-in Pinyin IME (Chinese input method)
// A self-contained pinyin -> hanzi input engine with a built-in common-word
// dictionary. Toggle with Ctrl+Space. Works in the editor, the terminal and
// every LVGL textarea (browser address bar, dialogs, rename box, ...).
#pragma once
#include "../platform.h"

namespace nefu {
namespace ime {

// Reset to the English mode.
void init();

// IME on/off state (Ctrl+Space toggles it).
bool enabled();
void set_enabled(bool on);
void toggle();

// Feed one key to the IME. Returns true when the key was consumed (pinyin
// composition / candidate selection / page flip / Ctrl+Space).
// When a candidate is committed, out_utf8 receives the UTF-8 text (NUL
// terminated) and out_max is its buffer size. out_utf8[0] == 0 means the
// event was consumed but produced no text.
bool handle_key(int keycode, char ascii, uint8_t mods, char* out_utf8, int out_max);

// Draw the candidate bar on top of everything (bottom-center of the screen).
void paint(struct Surface& fb);

} // namespace ime
} // namespace nefu
