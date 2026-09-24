// nefuOS text library — string search (KMP, Boyer-Moore-Horspool)
// KMP：预处理模式串的 next 表，O(n+m) 单遍扫描。
// BMH：坏字符启发式，平均最快的最简单工程实现。
#pragma once
#include <stddef.h>

namespace nefu {
namespace text {

// KMP：返回 pat 在 text 中第一次出现的位置，找不到返回 -1
int kmp_find(const char* text, const char* pat);

// KMP：统计 pat 在 text 中出现的次数（不重叠）
int kmp_count(const char* text, const char* pat);

// BMH：返回 pat 第一次出现位置（等价结果，算法不同）
int bmh_find(const char* text, const char* pat);

// 朴素算法（用于测试对照）
int naive_find(const char* text, const char* pat);

int search_self_test();

} // namespace text
} // namespace nefu
