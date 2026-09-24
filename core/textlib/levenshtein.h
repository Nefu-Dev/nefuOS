// nefuOS text library — edit distance (Levenshtein)
// 编辑距离：把字符串 A 变成字符串 B 所需的最少单字符操作次数
// （插入 / 删除 / 替换各计 1 次）。经典动态规划：dp[i][j] 表示
// A[0..i) 与 B[0..j) 的距离，状态转移只依赖三个邻格。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace text {

// 返回 A 与 B 的编辑距离（<=255 时可直接当 int 用）
int levenshtein(const char* a, const char* b);

// 返回编辑距离，且输出到 ops 的编辑脚本（可选，传 NULL 跳过）。
// 脚本字符含义：'=' 保持，'I' 插入，'D' 删除，'R' 替换。
// ops 必须至少有 (len_a + len_b + 1) 个字节的空间，脚本以 '\0' 结尾。
int levenshtein_script(const char* a, const char* b, char* ops);

// 相似度 0..1000（1000 为完全相同）：1000 * (1 - dist / max(la, lb))
int similarity1000(const char* a, const char* b);

// self test，返回失败断言数，0 表示全部通过
int levenshtein_self_test();

} // namespace text
} // namespace nefu
