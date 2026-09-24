// nefuOS compression library — Huffman implementation
#include "huffman.h"
#include "bitio.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

// 树节点：256 符号叶子 + 合并内部节点
struct HNode {
    int freq;
    int sym;         // 叶子符号（内部节点 -1）
    int left, right; // 子节点索引（-1 = 无）
};

// 码表：每个符号的码字与长度
struct Code { uint32_t bits = 0; int len = 0; };

// 码字低位 = 路径首位（与 BitWriter 的 LSB 优先一致）
static void build_codes(const HNode* nodes, int node, uint32_t bits, int len, Code* codes) {
    if (node < 0) return;
    if (nodes[node].sym >= 0) {
        codes[nodes[node].sym].bits = bits;
        codes[nodes[node].sym].len = len;
#ifdef HUFF_DBG2
        printf("DBGB s=%d len=%d bits=%x\n", nodes[node].sym, len, bits);
#endif
        return;
    }
    build_codes(nodes, nodes[node].left, bits, len + 1, codes);
    build_codes(nodes, nodes[node].right, bits | (1u << len), len + 1, codes);
}

int huffman_encode_debug(const unsigned char* in, int n, unsigned char* out, int outcap) {
    int r = huffman_encode(in, n, out, outcap);
    return r;
}

int huffman_encode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    if (n == 0) { out[0] = 0; return 1; }
    int freq[256];
    memset(freq, 0, sizeof(freq));
    for (int i = 0; i < n; i++) freq[in[i]]++;
    // 统计不同符号数
    int sym_count = 0;
    for (int s = 0; s < 256; s++) if (freq[s] > 0) sym_count++;
    // 建树：叶子 + 内部节点（最多 2*256-1 = 511）
    HNode nodes[512];
    int nn = 0;
    if (sym_count == 1) {
        // 单符号：表大小 1（2 字节）+ sym + 4 长度 + 原数据
        out[0] = 0;
        out[1] = 1;
        out[2] = (unsigned char)in[0];
        out[3] = (unsigned char)(n >> 24);
        out[4] = (unsigned char)(n >> 16);
        out[5] = (unsigned char)(n >> 8);
        out[6] = (unsigned char)n;
        if (n > outcap - 7) return -1;
        memcpy(out + 7, in, n);
        return 7 + n;
    }
    // 叶子入数组
    int heaparr[512];
    int hn = 0;
    for (int s = 0; s < 256; s++) if (freq[s] > 0) {
        heaparr[hn++] = nn;
        nodes[nn++] = HNode{ freq[s], s, -1, -1 };
    }
    // 每次挑两个最小
    int root = -1;
    while (hn > 1) {
        // 找最小
        int m1 = 0;
        for (int i = 1; i < hn; i++) if (nodes[heaparr[i]].freq < nodes[heaparr[m1]].freq) m1 = i;
        int a = heaparr[m1]; heaparr[m1] = heaparr[hn-1]; hn--;
        int m2 = 0;
        for (int i = 1; i < hn; i++) if (nodes[heaparr[i]].freq < nodes[heaparr[m2]].freq) m2 = i;
        int b = heaparr[m2]; heaparr[m2] = heaparr[hn-1]; hn--;
        int id = nn++;
        nodes[id] = HNode{ nodes[a].freq + nodes[b].freq, -1, a, b };
        heaparr[hn++] = id;
    }
    root = heaparr[0];
    // 生成码表
    Code codes[256];
    build_codes(nodes, root, 0, 0, codes);
#ifdef HUFF_DBG
    for (int s = 0; s < 256; s++) if (codes[s].len > 0)
        printf("DBGC s=%d(%c) len=%d bits=%x\n", s, s>=32&&s<127?s:'?', codes[s].len, codes[s].bits);
    for (int i = 0; i < nn; i++) printf("DBGN %d: f=%d sym=%d l=%d r=%d\n", i, nodes[i].freq, nodes[i].sym, nodes[i].left, nodes[i].right);
#endif
    // 写表：符号数 + 原始长度(4) + 每个符号（值、码长、码字）+ 编码位流
    int syms = 0;
    for (int s = 0; s < 256; s++) if (freq[s] > 0) syms++;
    out[0] = (unsigned char)(syms >> 8);     // 符号数（2 字节，支持 256）
    out[1] = (unsigned char)(syms & 0xFF);
    out[2] = (unsigned char)(n >> 24);
    out[3] = (unsigned char)(n >> 16);
    out[4] = (unsigned char)(n >> 8);
    out[5] = (unsigned char)n;
    int hdr = 6;
    for (int s = 0; s < 256; s++) {
        if (freq[s] > 0) {
            if (hdr + 6 > outcap) return -1;
            out[hdr++] = (unsigned char)s;
            out[hdr++] = (unsigned char)codes[s].len;
            out[hdr++] = (unsigned char)(codes[s].bits >> 24);   // 大端 4 字节码字
            out[hdr++] = (unsigned char)(codes[s].bits >> 16);
            out[hdr++] = (unsigned char)(codes[s].bits >> 8);
            out[hdr++] = (unsigned char)codes[s].bits;
        }
    }
    // 写码字（位流从表头之后开始，互不覆盖）
    BitWriter w(out + hdr, outcap - hdr);
    for (int i = 0; i < n; i++) {
        Code& c = codes[in[i]];
        w.write_bits(c.bits, c.len);
    }
    int total = hdr + w.finish();
    if (total > outcap) return -1;
    return total;
}

int huffman_decode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    if (n <= 0) return -1;
    int syms = (in[0] << 8) | in[1];
    if (syms == 1) {
        // 单符号格式：sym + 长度(4) + 原数据
        unsigned char sym = in[2];
        int m = ((int)in[3] << 24) | ((int)in[4] << 16) | ((int)in[5] << 8) | in[6];
        if (m < 0 || m > outcap) return -1;
        memset(out, sym, m);
        return m;
    }
    if (syms > 256 || n < 6 + syms * 6) return -1;
    // 读表（sym + len + 4 字节码字），表头含原始长度
    int orig_len = ((int)in[2] << 24) | ((int)in[3] << 16) | ((int)in[4] << 8) | in[5];
    if (orig_len < 0 || orig_len > outcap) return -1;
    int hdr = 6;
    // 按码字建树：root=0，沿码字位插入叶子
    HNode nodes[1024];
    nodes[0] = HNode{ 0, -1, -1, -1 };
    int nn = 1;
    for (int i = 0; i < syms; i++) {
        int s = in[hdr++];
        int len = in[hdr++];
        uint32_t bits = ((uint32_t)in[hdr++] << 24) | ((uint32_t)in[hdr++] << 16) |
                        ((uint32_t)in[hdr++] << 8) | (uint32_t)in[hdr++];
        int cur = 0;
        for (int b = 0; b < len; b++) {
            int bit = (bits >> b) & 1;
            int& child = bit ? nodes[cur].right : nodes[cur].left;
            if (b == len - 1) {
                if (child != -1) { printf("DBG conflict s=%d len=%d bits=%x b=%d\n", s, len, bits, b); return -1; }
                child = nn;
                nodes[nn] = HNode{ 1, s, -1, -1 };
                nn++;
            } else {
                if (child == -1) {
                    child = nn;
                    nodes[nn] = HNode{ 0, -1, -1, -1 };
                    nn++;
                }
                cur = child;
            }
        }
    }
    int root = 0;
    // 解码：沿树走位
    BitReader r(in + hdr, n - hdr);
    int o = 0;
    int cur = root;
    while (o < orig_len) {
        if (nodes[cur].sym >= 0) {
            out[o++] = (unsigned char)nodes[cur].sym;
            cur = root;
            continue;
        }
        if (r.eof()) { printf("DBG eof o=%d/%d pos=%d\n", o, orig_len, r.byte_pos()); return -1; }
        int b = r.read_bit();
        cur = b ? nodes[cur].right : nodes[cur].left;
        if (cur < 0) { printf("DBG badpath o=%d/%d b=%d\n", o, orig_len, b); return -1; }
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int huffman_self_test() {
    g_fails = 0;
    unsigned char enc[4096], dec[4096];
    // 经典文本：重复度高 → 压缩
    {
        const char* txt = "this is a test of huffman coding this is a test this is a test of huffman "
                           "huffman coding is a prefix code based on symbol frequencies "
                           "the more frequent the symbol the shorter the code "
                           "this longer text should compress well below its original size "
                           "huffman coding is a prefix code based on symbol frequencies "
                           "the more frequent the symbol the shorter the code "
                           "this is a test of huffman coding this is a test this is a test of huffman "
                           "huffman coding is a prefix code based on symbol frequencies "
                           "the more frequent the symbol the shorter the code "
                           "this longer text should compress well below its original size "
                           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                           "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                           "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
        int n = (int)strlen(txt);
        int el = huffman_encode((const unsigned char*)txt, n, enc, sizeof(enc));
        expect("huff-encode", el > 0);
        expect("huff-compresses", el < n);
        int dl = huffman_decode(enc, el, dec, sizeof(dec));
        expect("huff-decode", dl == n && memcmp(dec, txt, n) == 0);
    }
    // 单符号
    {
        unsigned char in[32];
        memset(in, 'Q', 32);
        int el = huffman_encode(in, 32, enc, sizeof(enc));
        expect("huff-single-encode", el > 0);
        int dl = huffman_decode(enc, el, dec, sizeof(dec));
        expect("huff-single-decode", dl == 32 && memcmp(dec, in, 32) == 0);
    }
    // 全部 256 符号
    {
        unsigned char in[256];
        for (int i = 0; i < 256; i++) in[i] = (unsigned char)i;
        int el = huffman_encode(in, 256, enc, sizeof(enc));
        expect("huff-all-encode", el > 0);
        int dl = huffman_decode(enc, el, dec, sizeof(dec));
        expect("huff-all-decode", dl == 256 && memcmp(dec, in, 256) == 0);
    }
    // 二进制数据
    {
        unsigned char in[300];
        for (int i = 0; i < 300; i++) in[i] = (unsigned char)((i * 31 + 7) % 251);
        int el = huffman_encode(in, 300, enc, sizeof(enc));
        expect("huff-bin-encode", el > 0);
        int dl = huffman_decode(enc, el, dec, sizeof(dec));
        expect("huff-bin-decode", dl == 300 && memcmp(dec, in, 300) == 0);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
