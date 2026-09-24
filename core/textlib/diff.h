// nefuOS text library — line-level diff (Myers-style, LCS based)
// 行级差异：把两段文本按行切分，输出增删行清单。
// 基于 LCS 的思想：保留最长公共子序列（未变化行），其余标记 +/-。
// 用于版本对比、日志对比等场景。
#pragma once

namespace nefu {
namespace text {

enum DiffOp { DIFF_KEEP = 0, DIFF_DEL = 1, DIFF_ADD = 2 };

struct DiffLine {
    int op;        // DIFF_KEEP / DIFF_DEL / DIFF_ADD
    int src_line;  // 原文件行号（0 起；ADD 时为 -1）
    int dst_line;  // 新文件行号（0 起；DEL 时为 -1）
};

// 计算行级 diff。原文本 a_lines 行、新文本 b_lines 行。
// 结果写入 out（最多 out_cap 项），返回总项数。
// 行内容由调用方持有（本函数只比较指针指向的字符串）。
int line_diff(const char* const* a, int a_lines,
              const char* const* b, int b_lines,
              DiffLine* out, int out_cap);

// 便捷：切分行（按 \n，忽略末尾空行），返回行数（最多 max_lines）
int split_lines(const char* text, const char** out, int max_lines);

int diff_self_test();

} // namespace text
} // namespace nefu
