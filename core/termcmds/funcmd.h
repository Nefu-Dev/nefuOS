// =============================================================================
//  funcmd.h —— 趣味命令: cowsay/fortune/sl/figlet/random/joke/quote
// =============================================================================
#pragma once

#include "termcmds_all.h"

namespace nefu {
namespace termcmds {

// ---- 纯算法 ----
// 把一段文本按宽度折行,追加到 out(每行不超过 width 列)。
void word_wrap(const char* text, int width, nefu::List<nefu::String>& out);

// figlet: 用内置 5x7 点阵字模把一行大写文本渲染成多行,追加到 out。
void figlet_render(const char* text, nefu::List<nefu::String>& out);

// 取第 idx 条 fortune 语录(循环)。
const char* fortune_get(int idx);
const char* joke_get(int idx);

int funcmd_self_test();

} // namespace termcmds
} // namespace nefu
