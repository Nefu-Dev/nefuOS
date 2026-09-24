// nefuOS compression library — LZW implementation
#include "lzw.h"
#include "bitio.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

namespace {
const int MAXDICT = 4096;   // 字典上限（GIF 风格）
const int CODEBITS = 12;    // 码宽
}

// 字典：前缀下标 + 尾字符，线性探测哈希
struct LZWEntry { int prefix; int ch; };
struct LZWDict {
    LZWEntry e[MAXDICT];
    int n;
    void clear() {
        n = 258;                                  // 256=CLEAR、257=EOI 保留
        for (int i = 0; i < 256; i++) { e[i].prefix = -1; e[i].ch = i; }
    }
    int find(int prefix, int ch) {
        for (int i = 258; i < n; i++)
            if (e[i].prefix == prefix && e[i].ch == ch) return i;
        return -1;
    }
    void add(int prefix, int ch) {
        if (n < MAXDICT) { e[n].prefix = prefix; e[n].ch = ch; n++; }
    }
};

int lzw_encode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    if (n == 0) return 0;
    LZWDict dict;
    dict.clear();
    BitWriter w(out, outcap);
    int prefix = in[0];
    int codebits = 9;                 // 字典 <512 用 9 位，之后 10/11/12
    for (int i = 1; i < n; i++) {
        int c = in[i];
        int idx = dict.find(prefix, c);
        if (idx >= 0) {
            prefix = idx;
        } else {
            w.write_bits((uint32_t)prefix, codebits);
            dict.add(prefix, c);
            // 码宽升级：下一码超过当前宽度上限时切换
            int next = dict.n;
            if (next > (1 << codebits) - 1 && codebits < CODEBITS) codebits++;
            prefix = c;
            if (dict.n >= MAXDICT - 1) {
                // 字典满：写入 256（重置码），重建（GIF 约定）
                w.write_bits(256u, codebits);
                dict.clear();
                codebits = 9;
                prefix = c;
            }
        }
    }
    w.write_bits((uint32_t)prefix, codebits);
    w.write_bits(257u, codebits);     // 结束码
    return w.finish();
}

int lzw_decode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    LZWDict dict;
    dict.clear();
    BitReader r(in, n);
    int o = 0;
    int codebits = 9;
    int prev = -1;
    unsigned char stack[4096];     // 解码栈：收集串后翻转输出
    for (;;) {
        int code = (int)r.read_bits(codebits);
        if (code == 257) break;                    // 结束码
        if (code == 256) {                         // 重置码
            dict.clear();
            codebits = 9;
            prev = -1;
            continue;
        }
        bool special = (code >= dict.n);           // LZW 经典特例：code==n
        int start = special ? prev : code;
        if (special && prev < 0) return -1;
        // 沿 prefix 链压栈（栈底 = 串首字符）
        int st = 0;
        int walk = start;
        while (walk >= 0 && st < 4096) { stack[st++] = (unsigned char)dict.e[walk].ch; walk = dict.e[walk].prefix; }
        if (special) stack[st++] = stack[st - 1];  // 补 prev 串首字符
        if (st == 0) return -1;
        // 翻转输出
        if (o + st > outcap) return -1;
        for (int k = st - 1; k >= 0; k--) out[o++] = stack[k];
        // 加入新字典项：prev 码号 + 当前串首字符（标准 LZW 同步规则）
        if (prev >= 0 && dict.n < MAXDICT) dict.add(prev, stack[st - 1]);
        prev = code;
        int next = dict.n;
        // 解码器字典项比编码器少 1 个（首码不 add），升级边界用 >= 对齐
        if (next >= (1 << codebits) - 1 && codebits < CODEBITS) codebits++;
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int lzw_self_test() {
    g_fails = 0;
    unsigned char enc[8192], dec[8192];
    // 文本
    {
        const char* txt = "TOBEORNOTTOBEORTOBEORNOT";
        int n = (int)strlen(txt);
        int el = lzw_encode((const unsigned char*)txt, n, enc, sizeof(enc));
        expect("lzw-encode", el > 0);
        int dl = lzw_decode(enc, el, dec, sizeof(dec));
        expect("lzw-decode", dl == n && memcmp(dec, txt, n) == 0);
    }
    // 重复
    {
        unsigned char in[300];
        memset(in, 'x', 300);
        int el = lzw_encode(in, 300, enc, sizeof(enc));
        expect("lzw-run-encode", el > 0);
        expect("lzw-run-small", el < 300);
        int dl = lzw_decode(enc, el, dec, sizeof(dec));
        expect("lzw-run-decode", dl == 300 && memcmp(dec, in, 300) == 0);
    }
    // 二进制
    {
        unsigned char in[500];
        for (int i = 0; i < 500; i++) in[i] = (unsigned char)((i * 29 + 5) % 251);
        int el = lzw_encode(in, 500, enc, sizeof(enc));
        expect("lzw-bin-encode", el > 0);
        int dl = lzw_decode(enc, el, dec, sizeof(dec));
        expect("lzw-bin-decode", dl == 500 && memcmp(dec, in, 500) == 0);
    }
    // 触发字典重置（>4095 不同串，约 4 万字符）
    {
        unsigned char in[8000];
        for (int i = 0; i < 8000; i++) in[i] = (unsigned char)((i * 13 + 5) % 256);
        int el = lzw_encode(in, 8000, enc, sizeof(enc));
        expect("lzw-big-encode", el > 0 && el < 4000);
        int dl = lzw_decode(enc, el, dec, sizeof(dec));
        expect("lzw-big-decode", dl == 8000 && memcmp(dec, in, 8000) == 0);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
