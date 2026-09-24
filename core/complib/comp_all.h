// nefuOS compression library — 聚合头（STL 风格）
// 一次 include 全部压缩模块。自测：comp_all_self_test() 汇总各模块失败数。
#pragma once
#include "complib/bitio.h"
#include "complib/rle.h"
#include "complib/huffman.h"
#include "complib/lz77.h"
#include "complib/lzw.h"
#include "complib/arithmetic.h"
#include "complib/bwt.h"

namespace nefu {
namespace comp {

inline int comp_all_self_test() {
    return bitio_self_test() + rle_self_test() + huffman_self_test() +
           lz77_self_test() + lzw_self_test() + arithmetic_self_test() +
           bwt_self_test();
}

} // namespace comp
} // namespace nefu
