// nefuOS text library — lightweight regex engine
// 迷你正则引擎：递归下降解析 + 回溯匹配。支持：
//   . 任意字符   [abc] 字符类   [^abc] 取反   a|b 或
//   * + ? 量词   {m,n} 区间量词   ^ $ 锚点   \d \w \s \D \W \S 转义
//   圆括号分组（无反向引用）
// 不支持：反向引用、前瞻/后瞻、Unicode 属性。教学用，规模小。
#pragma once

namespace nefu {
namespace text {

// 编译并匹配：返回是否匹配整个字符串（等价于 ^...$）
bool regex_match(const char* pattern, const char* text);

// 搜索：返回 pattern 在 text 中首次匹配的起始位置，找不到返回 -1
int regex_search(const char* pattern, const char* text);

// 匹配并输出捕获组（最多 8 组，含整体）到 out[0..8]，每组为
// [start,end) 区间；未参与匹配的组 start=-1。返回组数。
struct Capture { int start, end; };
int regex_capture(const char* pattern, const char* text, Capture* out, int cap);

int regex_self_test();

} // namespace text
} // namespace nefu
