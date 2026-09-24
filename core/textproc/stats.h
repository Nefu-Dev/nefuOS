// nefuOS 文本处理库 —— 统计 / 向量 / 可读性
//   - 词频统计(空白/标点切分，小写归一)
//   - n-gram(词级 / 字符级)
//   - TF-IDF、余弦相似度(词袋向量)
//   - 字符频率
//   - 可读性指数 Flesch Reading Ease / Flesch-Kincaid Grade
//   - 平均词长 / 平均句长
// 全部 ASCII 友好，new[]/delete[] 管理，无 STL/异常/RTTI。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// ---------------- 词频 ----------------
struct WordFreq {
    char** words;   // 词(新分配)
    int*   counts;
    int    n;
};

// 对 text 做空白/标点切词并统计词频，返回新分配的 WordFreq。
WordFreq* word_freq(const char* text);
void      word_freq_free(WordFreq* wf);
// 生成 "word: count" 每行一条的报告(新分配字符串，调用方 delete[])。
char*     word_freq_report(const WordFreq* wf);
// 查某个词的频次(大小写不敏感)。
int       word_freq_get(const WordFreq* wf, const char* word);

// ---------------- n-gram ----------------
// 词级 n-gram：把 text 切词后把相邻 n 个词用空格拼成一个 gram。
// 返回新分配字符串数组，数量写进 out_count，调用方 delete[] 每一项与数组。
char** ngram_words(const char* text, int n, int* out_count);
// 字符级 n-gram(在小写字母序列上)。
char** ngram_chars(const char* text, int n, int* out_count);
// 释放上面返回的字符串数组。
void   ngram_free(char** grams, int count);

// ---------------- 向量 / 相似度 ----------------
// 词袋余弦相似度，返回 [0,1]。相同文本为 1.0。
double cosine_similarity(const char* docA, const char* docB);
// 简化 TF-IDF：在 docs[0..ndoc-1] 这个小语料里，求 term 在 docIdx 文档的
// tf-idf 分数。term 大小写不敏感。
double tfidf_score(const char* const* docs, int ndoc, int docIdx, const char* term);

// ---------------- 字符频率 / 可读性 ----------------
// 把 text 的字符频率填入 counts[0..255](调用方保证大小 256)。
void char_frequency(const char* text, int* counts256);
int  count_sentences(const char* text);   // . ! ? 计数
int  count_words_tp(const char* text);    // 空白/标点分词后的词数
double avg_word_length(const char* text);
double avg_sentence_length(const char* text);
// Flesch Reading Ease(越高越好读)与 Flesch-Kincaid 年级，写入 out_*。
void flesch(const char* text, double* out_ease, double* out_grade);

// 自动化可读性指数 ARI：4.71*字符数/词数 + 0.5*词数/句数 - 21.43。
double automated_readability_index(const char* text);
// Gunning Fog 指数(近似)：0.4*(ASL + 100*复杂词比例)。
double gunning_fog(const char* text);
// 类符/形符比 TTR：unique 词数 / 总词数，衡量词汇多样性。
double type_token_ratio(const char* text);

// 极简文本分类：给定若干类别的关键词表，统计命中数，返回最高分类别下标。
//   categories[k] 是该类别的关键词(逗号分隔)。返回 -1 表示无命中。
int classify_best(const char* text, const char* const* categories, int ncat);

// 自检：返回失败数(0 = 全绿)。
int stats_self_test();

} // namespace textproc
} // namespace nefu
