// nefuOS text library — longest common subsequence (LCS)
// 最长公共子序列：不要求连续，但保持相对顺序。
// 例：LCS("ABCBDAB", "BDCABA") = 4（"BCBA" 或 "BDAB"）。
// 经典二维 DP，可输出一条具体子序列。
#pragma once
#include <stddef.h>

namespace nefu {
namespace text {

// 返回 LCS 长度
int lcs_len(const char* a, const char* b);

// 返回 LCS 长度并把一条最长公共子序列写入 out（以 '\0' 结尾）。
// out 至少需要 min(la, lb) + 1 字节。
int lcs_get(const char* a, const char* b, char* out);

// 两个字符串的 LCS 相似度 0..1000
int lcs_similarity1000(const char* a, const char* b);

int lcs_self_test();

} // namespace text
} // namespace nefu
