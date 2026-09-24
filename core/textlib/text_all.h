// nefuOS text library — aggregate header (STL-style)
// 聚合头：一次引入全部文本处理模块，并提供整体自检入口。
// 用法： #include "textlib/text_all.h"
#pragma once

#include "textlib/levenshtein.h"
#include "textlib/lcs.h"
#include "textlib/kmp.h"
#include "textlib/aho.h"
#include "textlib/token.h"
#include "textlib/ngram.h"
#include "textlib/regexlite.h"
#include "textlib/diff.h"

namespace nefu {
namespace text {

// 运行全部文本模块自检，返回总失败数（0 = 全部通过）
inline int text_all_self_test() {
    return levenshtein_self_test() +
           lcs_self_test() +
           search_self_test() +
           aho_self_test() +
           token_self_test() +
           ngram_self_test() +
           regex_self_test() +
           diff_self_test();
}

} // namespace text
} // namespace nefu
