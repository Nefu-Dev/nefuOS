// nefuOS text library — n-gram & text statistics
// n-gram：相邻 n 个字符（或词）构成的片段，用于文本相似度、
// 语言模型特征、拼写纠错提示等。本模块同时提供通用文本统计：
// 字符数/单词数/行数/字节分布/词频 TopK。
#pragma once
#include <stdint.h>

namespace nefu {
namespace text {

struct TextStats {
    int bytes;       // 总字节数
    int chars;       // 字符数（UTF-8，简化：CJK 三字节算 1，其余 1）
    int words;       // 英文单词数
    int lines;       // 行数
    int cjk_chars;   // CJK 字符数
    int spaces;      // 空白字节数
    int digits;      // 数字字节数
};

// 统计文本
void text_stats(const char* text, TextStats* st);

// 把文本切成字符 n-gram（UTF-8 感知：CJK 按单字），写入 out（最多 out_cap），
// 返回 n-gram 数量。每项是 [start,end) 字节区间，长度固定为 span 字节。
struct Gram {
    int start, end;
};
int char_ngrams(const char* text, int n, Gram* out, int out_cap);

// 词频 TopK：按出现次数排序的前 k 个单词（小写化）。写入 out_words（每项
// 一个 '\0' 结尾单词，cap_words 表示数组容量、cap_wordlen 单词最大长度），
// 返回实际词数。
struct WordFreq { int count; };
int top_words(const char* text, int k, char (*out_words)[32], int* out_counts, int cap_words);

int ngram_self_test();

} // namespace text
} // namespace nefu
