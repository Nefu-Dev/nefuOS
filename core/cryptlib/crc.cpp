// nefuOS crypto library — CRC implementation
#include "crc.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

// 查表法：每字节处理 8 位。表 = 多项式对 0..255 的"余数"预计算
static uint32_t crc32_table[256];
static uint64_t crc64_table[256];
static int crc_tables_ready = 0;

static void crc_build_tables() {
    if (crc_tables_ready) return;
    for (int i = 0; i < 256; i++) {
        // CRC32：反射多项式 0xEDB88320
        uint32_t c = (uint32_t)i;
        for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
        // CRC64：反射多项式 0xC96C5795D7870F42
        uint64_t c64 = (uint64_t)i;
        for (int j = 0; j < 8; j++) c64 = (c64 & 1) ? (0xC96C5795D7870F42ull ^ (c64 >> 1)) : (c64 >> 1);
        crc64_table[i] = c64;
    }
    crc_tables_ready = 1;
}

uint32_t crc32(const unsigned char* data, int n) {
    crc_build_tables();
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t crc32_str(const char* s) {
    return crc32((const unsigned char*)s, (int)strlen(s));
}

uint64_t crc64(const unsigned char* data, int n) {
    crc_build_tables();
    uint64_t c = 0xFFFFFFFFFFFFFFFFull;
    for (int i = 0; i < n; i++) c = crc64_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFFFFFFFFFull;
}

uint64_t crc64_str(const char* s) {
    return crc64((const unsigned char*)s, (int)strlen(s));
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int crc_self_test() {
    g_fails = 0;
    // 标准向量："123456789" 的 CRC32 = 0xCBF43926（IEEE）
    expect("crc32-123456789", crc32_str("123456789") == 0xCBF43926u);
    expect("crc32-empty", crc32((const unsigned char*)"", 0) == 0);
    // "abc" 的 CRC32 = 0x352441C2
    expect("crc32-abc", crc32_str("abc") == 0x352441C2u);
    // CRC64："123456789" = 0xE9C6D914C4B8D9CA（ECMA-182）
    expect("crc64-123456789", crc64_str("123456789") == 0x995DC9BBDF1939FAull);
    // 基本性质：相同数据一致，不同数据（大概率）不同
    expect("crc-deterministic", crc32_str("hello") == crc32_str("hello"));
    expect("crc-differs", crc32_str("hello") != crc32_str("hellp"));
    // 追加数据改变结果
    expect("crc-append", crc32_str("hello world") != crc32_str("hello"));
    return g_fails;
}

} // namespace crypt
} // namespace nefu
