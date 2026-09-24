// nefuOS 文本处理库 —— 编辑距离与序列相似性
// 本模块从零实现经典的串距离度量，全部基于显式 new[]/delete[] 缓冲，
// 不依赖 STL / 异常 / RTTI，可同时在宿主(g++)与裸机 freestanding 下编译。
//
// 包含：
//   - Levenshtein 编辑距离(插入/删除/替换，带回溯对齐)
//   - Damerau-Levenshtein(允许相邻两字符交换，最优串对齐版本)
//   - Hamming 汉明距离(等长串逐位不同计数)
//   - Jaro / Jaro-Winkler 相似度(短串、人名模糊匹配)
//   - 最长公共子序列 LCS(长度 + 重构字符串)
//   - 最短公共超序列 SCS(长度 + 重构字符串)
//
// 复杂度约定均在函数注释中标出；每个公开算法都在 editdist_self_test()
// 里用教科书已知值做了交叉校验。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// -------------------------------------------------------------------------
// Levenshtein 编辑距离
//   把 a 变成 b 所需的最少单字符插入/删除/替换次数，每次代价均为 1。
//   DP: dp[i][j] = a[:i] 与 b[:j] 的距离。
//   时间 O(|a|*|b|)，空间这里优化为两行 O(min(|a|,|b|))。
//   例: levenshtein("kitten","sitting") == 3
// -------------------------------------------------------------------------
int levenshtein(const char* a, const char* b);

// 完整版 Levenshtein，保留整张 DP 表并回溯，输出人类可读的对齐报告。
// 返回一块调用方负责 delete[] 的新字符串，形如：
//   a:  ki-tten
//   b:  sitting
//   ops: DDSSMMM  (M=匹配 S=替换 D=删除 I=插入)
// 入参为 NULL 安全；空串安全。
char* levenshtein_align(const char* a, const char* b);

// Damerau-Levenshtein(最优串对齐 / restricted edit distance)
//   在 Levenshtein 基础上额外允许一次相邻两字符的交换(transposition)。
//   这里实现的是“限制版”：一个字符最多被编辑一次，避免多次交换叠加。
//   时间 O(|a|*|b|)，空间 O(|a|*|b|)。
int damerau_levenshtein(const char* a, const char* b);

// Hamming 汉明距离
//   仅适用于等长串：逐位置比较，统计不同字符数。
//   若两串长度不同，返回 -1(调用方可据此判定不可比)。
//   例: hamming("karolin","kathrin") == 3
int hamming(const char* a, const char* b);

// Jaro 相似度，返回 [0,1] 之间(越大越相似)。
//   m = 匹配字符数(在窗口 floor(maxlen/2)-1 内)
//   t = 换位对数 / 2
//   J = (m/|a| + m/|b| + (m-t)/m) / 3
double jaro(const char* a, const char* b);

// Jaro-Winkler 相似度：在 Jaro 基础上奖励共同前缀(最多 4 个字符)，
//   默认前缀权重 p=0.1，常用于人名匹配。
//   例: jaro_winkler("MARTHA","MARHTA") ≈ 0.9611
double jaro_winkler(const char* a, const char* b);

// -------------------------------------------------------------------------
// 最长公共子序列 LCS(Longest Common Subsequence)
//   子序列不要求连续。dp[i][j] = a[:i] 与 b[:j] 的 LCS 长度。
//   时间 O(|a|*|b|)，空间 O(|a|*|b|)。
//   例: lcs_length("ABCBDAB","BDCAB") == 4
// -------------------------------------------------------------------------
int lcs_length(const char* a, const char* b);

// 重构并返回 LCS 字符串(调用方 delete[])。
//   例: strcmp(lcs_string("ABCBDAB","BDCAB"), "BCAB") == 0
char* lcs_string(const char* a, const char* b);

// 最短公共超序列 SCS(Shortest Common Supersequence)长度。
//   由 LCS 可证: |SCS| = |a| + |b| - |LCS|。
int scs_length(const char* a, const char* b);

// 重构并返回 SCS 字符串(调用方 delete[])：一个同时以 a、b 为子序列的
// 最短字符串。
char* scs_string(const char* a, const char* b);

// 归一化相似度 [0,1]：1 - levenshtein / max(|a|,|b|)，供 UI 直接显示。
float similarity_ratio(const char* a, const char* b);

// -------------------------------------------------------------------------
// 序列对齐的其它经典度量
// -------------------------------------------------------------------------
// 最长公共子串(连续)长度；区别于 LCS(可不连续)。
int  longest_common_substring_len(const char* a, const char* b);
// 重构最长公共子串(调用方 delete[])。
char* longest_common_substring_str(const char* a, const char* b);

// Needleman-Wunsch 全局对齐打分(匹配 +1，不匹配 -1，空位 penalty)。
int  needleman_wunsch_score(const char* a, const char* b, int match, int mismatch, int gap);

// Smith-Waterman 局部对齐最高分(找两段中最相似的连续片段)。
int  smith_waterman_score(const char* a, const char* b);

// 词集合 Jaccard 相似度 [0,1]：把两文本按空白切词后求 |交集|/|并集|。
double jaccard_words(const char* a, const char* b);

// Dice 系数 [0,1]：2*|交集|/(|A|+|B|)，词级。
double dice_words(const char* a, const char* b);

// 自检：用教科书已知值校验全部算法，返回失败数(0 = 全绿)。
int editdist_self_test();

} // namespace textproc
} // namespace nefu
