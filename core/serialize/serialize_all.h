// nefuOS 数据序列化与编解码库 —— 聚合头
// 一次 include 即可使用全部子模块；并提供 serialize_self_test() 汇总入口。
#pragma once

#include "textfmt.h"     // CSV / INI / XML / TOML / JSON5 / properties / env / s-expr
#include "binary.h"      // MessagePack / Bencode / UBJSON / CBOR / BSON / varint
#include "imagec.h"      // BMP / TGA / PPM / PGM / PBM / QOI / PCX / ICO
#include "audiof.h"      // WAV / AIFF / AU / raw PCM

namespace nefu {
namespace serialize {

// 运行所有子模块的 self_test，返回失败总数（0 == 全部通过）。
int serialize_self_test();

} // namespace serialize
} // namespace nefu

// 聚合头：一次性 include 四个子模块，调用 serialize_self_test() 跑全部往返测试。