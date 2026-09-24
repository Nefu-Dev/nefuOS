// =============================================================================
//  textcmd.h —— 文本处理命令: grep/sed/awk/sort/uniq/wc/head/tail/rev/cat
// -----------------------------------------------------------------------------
//  这些命令的"纯算法"部分也放在头里 inline 暴露,便于 self_test 直接断言:
//    * grep_line_match   —— 一行是否匹配模式(支持忽略大小写)
//    * wc_count          —— 统计行/词/字符数
//    * sort_lines        —— 对行列表排序(字典序/数值序,可反转)
//    * sed_substitute    —— 行内替换(可全局)
//    * awk_field         —— 按分隔符取第 N 个字段
// =============================================================================
#pragma once

#include "termcmds_all.h"
#include "filecmd.h"

namespace nefu {
namespace termcmds {

// ---- 纯算法(无输出副作用,直接测) ----
// 行匹配: icase=true 时大小写不敏感。返回是否命中。
bool grep_line_match(const char* line, const char* pattern, bool icase);

// 统计: 行数以 '\n' 计; 词以连续非空白串计; 字符即字节数。
void wc_count(const char* text, int* out_lines, int* out_words, int* out_chars);

// 就地排序 lines; numeric=true 按整数大小, reverse=true 反序。
void sort_lines(nefu::List<nefu::String>& lines, bool numeric, bool reverse);

// 在 line 中把 old 替换为 new; global=true 替换全部,否则只替换第一处。
nefu::String sed_substitute(const char* line, const char* old, const char* nw, bool global);

// 按 sep(空白连续序列为一个分隔符; 或指定字符) 取第 field 个字段(1-based)。
nefu::String awk_field(const char* line, char sep, int field);

// 把文本按行读入: 有文件参数读 VFS, 否则读 g_term_stdin。
// 返回读到的行列表(追加到 out)。失败返回 false。
bool text_load_lines(int argc, const char** argv, int file_arg_index,
                     nefu::List<nefu::String>& out, TermOutput* err);

int textcmd_self_test();

} // namespace termcmds
} // namespace nefu
