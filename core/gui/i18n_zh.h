// i18n_zh.h - exact-match English -> Chinese table for the draw layer.
#pragma once

namespace nefu {

// Returns the Chinese rendering of a UI string when the language is
// Chinese and the string is a known UI phrase; otherwise returns the
// input pointer untouched (cheap and allocation-free).
const char* zh_ui(const char* en);

} // namespace nefu
