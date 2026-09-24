// nefuOS 文本处理库 —— 多模式字符串搜索与回文/后缀结构
// 从零实现经典串匹配算法，全部显式 new[]/delete[]，无 STL/异常/RTTI。
//
// 包含：
//   - KMP 自动机(最坏 O(n+m))
//   - Boyer-Moore(坏字符 + 好后缀，O(n) 最坏)
//   - Boyer-Moore-Horspool(简化版，实践更快)
//   - Rabin-Karp(滚动哈希，多模式友好，O(n+m) 平均)
//   - Z 算法(构造 Z 数组 + 用 Z 做模式匹配)
//   - Manacher(线性求最长回文子串)
//   - Aho-Corasick 多模式自动机(一次扫描匹配全部模式)
//   - 后缀数组 SA-IS 的简化版(倍增法 O(n log n)) + Kasai LCP
//   - 简化后缀自动机 SAM(子串存在性 / 不同子串计数)
//
// 约定：所有"返回数组"的函数都新分配一块 int[]，长度通过 out_count 带出，
// 调用方负责 delete[]。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// -------------------------------------------------------------------------
// KMP：先对模式求 next(失败函数)，再扫描文本。
//   返回所有命中起始下标(新分配 int[])，数量写进 out_count。
//   例: kmp_first("ABABC","ABABABC") == 2
// -------------------------------------------------------------------------
int* kmp_search(const char* pat, const char* text, int* out_count);
int  kmp_first(const char* pat, const char* text);   // -1 表示未命中
void kmp_build_failure(const char* pat, int* fail_out); // fail[i] = 最长真前后缀长度

// Boyer-Moore：坏字符表 + 好后缀规则。返回所有命中起始下标。
int* bm_search(const char* pat, const char* text, int* out_count);

// Boyer-Moore-Horspool：只用坏字符(最后一位)，实现简洁、缓存友好。
int* horspool_search(const char* pat, const char* text, int* out_count);

// Rabin-Karp：滚动哈希(基数 256, 模一个大质数)。
int* rabin_karp_search(const char* pat, const char* text, int* out_count);

// Z 算法：z_out[i] = s 与 s[i:] 的最长公共前缀长度，z_out[0] 定义为 |s|。
void z_build(const char* s, int* z_out);
// 用 Z 算法匹配模式：在 text 末尾拼 '\1' 再接 pat 构造 Z 数组。
int* z_search(const char* pat, const char* text, int* out_count);

// Manacher：线性求 s 的最长回文子串。返回新分配字符串(调用方 delete[])。
//   例: manacher_longest("babad") 可能是 "bab" 或 "aba"。
char* manacher_longest(const char* s);
int   manacher_longest_len(const char* s);

// -------------------------------------------------------------------------
// Aho-Corasick 多模式匹配
//   pats[0..npat-1] 为模式串。search 返回扁平的命中对数组：
//   每命中一次输出两个 int (pattern_index, start_position)，共 2*count 个。
//   时间 O(text + sum(|pat|) + hits)。
// -------------------------------------------------------------------------
int* ac_search(const char* const* pats, int npat, const char* text,
               int* out_pair_count);

// -------------------------------------------------------------------------
// 后缀数组(倍增法 doubling，O(n log n))
//   返回 sa[0..n-1]：sa[k] 表示字典序第 k 小的后缀的起始下标。
//   调用方 delete[]。
// -------------------------------------------------------------------------
int* suffix_array(const char* s);
// Kasai 算法由 SA 求高度数组 rank[] 的相邻 LCP：
//   lcp[i] = sa[i] 与 sa[i-1] 的最长公共前缀长度(调用方 delete[])。
int* lcp_array(const char* s, const int* sa);

// 简化后缀自动机：判断 query 是否作为子串出现在 text 中。
bool sam_contains(const char* text, const char* query);
// 统计 text 的不同子串个数。
long sam_distinct_substrings(const char* text);
// 统计 query 作为子串在 text 中出现的次数(允许重叠)。
int  sam_count_occurrences(const char* text, const char* query);

// Shift-Or(bitap)：位并行精确匹配，模式长度 <= 63。
//   返回所有命中起始下标。
int* shift_or_search(const char* pat, const char* text, int* out_count);

// 自检：返回失败数(0 = 全绿)。
int search_self_test();

} // namespace textproc
} // namespace nefu
