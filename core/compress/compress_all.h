// nefuOS 压缩算法库 —— 聚合头
//
// 包含本库所有公开模块，并提供 compress_self_test() 一键汇总
// 运行所有子模块的 round-trip 自测，返回总失败数（0 表示全部通过）。
#pragma once
#include "rle.h"
#include "huffman.h"
#include "lz.h"
#include "bwt.h"
#include "arith.h"
#include "dict.h"
#include "other.h"

namespace nefu {
namespace compress {

// 依次运行各子模块自测，返回失败总数（0=全部通过）
int compress_self_test();

} // namespace compress
} // namespace nefu
