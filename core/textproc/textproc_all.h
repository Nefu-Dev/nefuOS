// nefuOS 文本处理库 —— 聚合头(STL 风格单 include)
//   #include "textproc/textproc_all.h"
// 暴露 nefu::textproc 下全部子模块，并提供 textproc_self_test()。
#pragma once

#include "editdist.h"
#include "search.h"
#include "trie.h"
#include "stats.h"
#include "phonetic.h"
#include "tokenize.h"

namespace nefu { namespace textproc {

// 汇总自检：返回所有子模块失败数之和(0 = 全部通过)。
inline int textproc_self_test() {
    int f = 0;
    f += editdist_self_test();
    f += search_self_test();
    f += trie_self_test();
    f += stats_self_test();
    f += phonetic_self_test();
    f += tokenize_self_test();
    return f;
}

}} // namespace
