// nefuOS 文本处理库 —— 语音编码(phonetic encoding)
//   把单词映射成按发音归类的代码，用于人名/地名模糊匹配。
//   - Soundex(US 人口普查)
//   - Metaphone(主码，简化版)
//   - Double Metaphone(主/次双码，简化版)
//   - NYSIIS(纽约州身份识别信息系统)
//   - Caverphone(新西兰地名)
//   - phonetic_match：两个词发音是否相近(Soundex 码相同)
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// Soundex：写出形如 "R163" 的 4 字符码到 out(调用方保证 out 容量 >=5)。
//   soundex("Robert")=="R163", soundex("Rupert")=="R163"
void soundex(const char* name, char* out);

// Metaphone 主码：写出最多 maxlen 字符(截断)，out 以 NUL 结尾。
void metaphone(const char* word, char* out, int maxlen);

// Double Metaphone：同时给出主码 primary 与次码 secondary。
void double_metaphone(const char* word, char* primary, char* secondary, int maxlen);

// NYSIIS：写出归一化语音码。
void nysiis(const char* word, char* out, int maxlen);

// Caverphone v1：写出 10 位数字串(近似)。
void caverphone(const char* word, char* out11);

// 语音匹配：Soundex 码相同即认为发音相近。
bool phonetic_match(const char* a, const char* b);

// 自检：返回失败数(0 = 全绿)。
int phonetic_self_test();

} // namespace textproc
} // namespace nefu
