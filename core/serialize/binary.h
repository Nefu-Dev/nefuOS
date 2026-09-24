// nefuOS 数据序列化与编解码库 —— 二进制格式模块
// binary.h: varint/zigzag / MessagePack / Bencode / UBJSON / CBOR / BSON(子集)
//
// 这些二进制格式都围绕同一棵 VValue 树（定义在 textfmt.h）做双向编解码：
//   * 打包(pack/write)：VValue -> 字节流
//   * 解包(unpack/parse)：字节流 -> VValue*（调用者 free_value 释放）
#pragma once
#include "../klib/klib.h"
#include "textfmt.h"   // 复用 VValue / VMember / free_value

namespace nefu {
namespace serialize {

// ============================================================================
// 0. varint / zigzag（LEB128 无符号 + 有符号变长编码）
//    varint：每字节低 7 位为数据，最高位为“后续还有”标志（小端优先）。
//    zigzag：把有符号整数映射为无符号，使小绝对值（含负数）编码更紧凑：
//            encode(n) = (n << 1) ^ (n >> 63)；decode 反推。
// ============================================================================
// 编码到 out（需至少 10 字节空间），返回写入字节数。
int      varint_encode(uint64_t v, uint8_t* out);
// 从 p 解码，*consumed 输出读取字节数；返回值。
uint64_t varint_decode(const uint8_t* p, int* consumed);
// 计算 varint 编码需要的字节数
int      varint_size(uint64_t v);

uint64_t zigzag_encode(int64_t v);
int64_t  zigzag_decode(uint64_t v);

int varint_self_test();

// ============================================================================
// 通用动态字节缓冲（二进制格式打包用）
// ============================================================================
struct ByteBuf {
    uint8_t* data;
    int len;
    int cap;
    ByteBuf();
    ~ByteBuf();
    void put8(uint8_t v);
    void put16(uint16_t v);          // 小端
    void put32(uint32_t v);          // 小端
    void put64(uint64_t v);          // 小端
    void put(const uint8_t* p, int n);
    void put_str(const char* s);     // 附加原始字节（不含长度）
    void reserve(int need);
private:
    ByteBuf(const ByteBuf&);
    ByteBuf& operator=(const ByteBuf&);
};

// 读取游标（边界检查，越界后 ok=false）
struct Cursor {
    const uint8_t* p;
    int len;
    int pos;
    bool ok;
    Cursor(const uint8_t* data, int n) : p(data), len(n), pos(0), ok(true) {}
    uint8_t  get8();
    uint16_t get16();   // 小端
    uint32_t get32();   // 小端
    uint64_t get64();   // 小端
    uint64_t get_be32(); // 大端
    uint64_t get_be64();
    void take(int n);
    const uint8_t* ptr(int n);
    bool eof() const { return pos >= len; }
};

// ============================================================================
// 1. MessagePack（https://msgpack.org）
//    覆盖：nil/bool/int(正小整数/负小整数/uint8/16/32/64,int8..64)/
//          float64/str/bin/array/map。不扩展 ext 类型。
// ============================================================================
void mp_pack(const VValue& v, ByteBuf& out);
// 解包整段数据；失败返回 0。
VValue* mp_unpack(const uint8_t* data, int len, const char** err = 0);
int     msgpack_self_test();

// ============================================================================
// 2. Bencode（BitTorrent 编码）
//    语法：i<int>e 整数 / <len>:<bytes> 字符串 / l<items>e 列表 /
//          d<key-value...>e 字典（键按字典序排序，键为字符串）。
// ============================================================================
bool bencode_write(const VValue& v, ByteBuf& out);
VValue* bencode_parse(const uint8_t* data, int len, const char** err = 0);
int     bencode_self_test();

// ============================================================================
// 3. UBJSON（Universal Binary JSON）—— 强类型容器子集
//    覆盖：null, bool, int(i/u 1/2/4/8), float(d), string(S), array([), object({)
// ============================================================================
bool ubjson_write(const VValue& v, ByteBuf& out);
VValue* ubjson_parse(const uint8_t* data, int len, const char** err = 0);
int     ubjson_self_test();

// ============================================================================
// 4. CBOR（RFC 8949）—— 子集
//    覆盖：major type 0(无符号)/1(负)/2(字节串)/3(字符串)/4(数组)/5(映射)/
//          7(浮点/简单值)；含附加值 n(0..23,8,9,10,11,12,13,14,15,16,17,18,19,20)。
// ============================================================================
bool cbor_write(const VValue& v, ByteBuf& out);
VValue* cbor_parse(const uint8_t* data, int len, const char** err = 0);
int     cbor_self_test();

// ============================================================================
// 5. BSON（二进制 JSON，MongoDB 文档）—— 子集
//    文档：int32 总长 + 元素序列 + 0x00；元素类型字节 + 名字\0 + 值。
//    支持：\x01 double, \x02 string, \x08 bool, \x0A null,
//          \x10 int32, \x12 int64, \x04 array, \x07 binary。
// ============================================================================
bool bson_write(const VValue& doc, ByteBuf& out);
VValue* bson_parse(const uint8_t* data, int len, const char** err = 0);
int     bson_self_test();

// 汇总
int binary_self_test();

} // namespace serialize
} // namespace nefu

// binary.h 汇总：varint/zigzag + MessagePack/Bencode/UBJSON/CBOR/BSON(子集)。
// 全部编解码围绕 VValue 树，字节缓冲用 ByteBuf。