// nefuOS 文本处理库 —— 分词与归一化
//   - 大小写归一(全小写 / 全大写)
//   - 词干提取(Porter 词干简化版：去 -ing/-ed/-ly/-es/-s/-ment 等后缀)
//   - 停用词过滤(the/a/an/is/...)
//   - 句子分割(. ! ?)
//   - 空白分词 + 二元语法(bigram)切分
//   - 正向最大匹配分词(基于内置小词典)
//   - 基本 UTF-8 解码(统计码点、读单字符)
// 面向 ASCII + 基本 UTF-8；全部 new[]/delete[]，无 STL/异常/RTTI。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// 大小写归一：返回新分配字符串。
char* to_lower_copy(const char* s);
char* to_upper_copy(const char* s);

// 句子分割：按 . ! ? 切分，返回句子字符串数组(去掉首尾空白)。
char** split_sentences(const char* text, int* out_count);

// 空白/标点分词：只保留字母数字，全部小写，返回词数组。
char** tokenize_words(const char* text, int* out_count);

// 释放上面任一返回的字符串数组。
void   free_str_array(char** arr, int n);

// 停用词过滤：返回去掉停用词后的新数组。
char** remove_stopwords(char** words, int n, int* out_count);
bool   is_stopword(const char* word);

// Porter 词干简化：把 word 的词干写入 out(调用方保证容量 >= len+1)。
//   例: stem_simple("running") -> "run", stem_simple("cats") -> "cat"
void   stem_simple(const char* word, char* out);

// 完整 Porter 词干算法(Martin Porter 1980 步骤 1a..5b)。
//   out 容量 >= 128。
void   porter_stem(const char* word, char* out);

// 二元语法：把 tokenize_words 的相邻词两两组合成 "w1 w2"。
char** bigram_tokenize(const char* text, int* out_count);

// 正向最大匹配：在内置词典里贪婪取最长词。words 输出到 bufs。
//   max_out/bufsz 由调用方提供，返回切出的词数。
int    max_match_segment(const char* text, char** bufs, int max_out, int bufsz);

// ---------------- UTF-8 基础 ----------------
// 首字节决定的序列长度(1..4)；非法字节返回 1。
int    utf8_seq_len(unsigned char c);
// 解码 s 处一个码点，写入 *cp，返回消耗字节数；非法返回 1 且 cp='?'。
int    utf8_decode(const char* s, int* cp);
// 文本中 UTF-8 码点个数(而非字节数)。
int    utf8_codepoint_count(const char* s);

// 自检：返回失败数(0 = 全绿)。
int tokenize_self_test();

} // namespace textproc
} // namespace nefu
