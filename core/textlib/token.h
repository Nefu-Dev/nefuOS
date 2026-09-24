// nefuOS text library — tokenizer (word segmentation)
// 分词器：按 Unicode 字节流把文本切成词元（英文单词、连续数字、
// 单个 CJK 字符、标点符号、空白）。返回 token 的起止位置，
// 不复制字符串，调用方自行引用原文。
#pragma once
#include <stddef.h>

namespace nefu {
namespace text {

enum TokenKind {
    TOK_WORD = 1,    // 字母串（a-zA-Z，可含 ' 与 -）
    TOK_NUM = 2,     // 数字串（0-9，可含小数点）
    TOK_CJK = 3,     // 单个中日韩字符（UTF-8 三字节）
    TOK_SYM = 4,     // 其他可见符号（一个字符）
    TOK_WS = 5       // 空白（连续空白合并为一个）
};

struct Token {
    int kind;
    int start;   // 字节偏移（含）
    int end;     // 字节偏移（不含）
    // 便捷：复制出 '\0' 结尾文本（buf 至少 end-start+1 字节）
    void copy(const char* text, char* buf) const;
};

// 切分整个文本，结果写入 out（最多 out_cap 个），返回 token 数
int tokenize(const char* text, Token* out, int out_cap);

// 把 UTF-8 序列按 token 种类重新排版输出（用 '|' 分隔），写入 buf
void tokenize_preview(const char* text, char* buf, int buf_cap);

int token_self_test();

} // namespace text
} // namespace nefu
