// nefuOS 压缩算法库 —— LZ77 家族字典压缩实现
// 见 lz.h。每个算法都给教学注释，底部自测覆盖多种输入。
#include "lz.h"
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

int lzw_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    uint16_t* prefix = new uint16_t[4096];
    uint8_t*  nch    = new uint8_t[4096];
    int next = 256;
    BitSink w(out + 4, cap - 4);
    int pref = in[0];
    for (int i = 1; i < n; i++) {
        int c = in[i];
        int found = -1;
        for (int code = 256; code < next; code++) {
            if (prefix[code] == (uint16_t)pref && nch[code] == (uint8_t)c) { found = code; break; }
        }
        if (found >= 0) {
            pref = found;
        } else {
            w.write_bits((uint32_t)pref, 12);
            if (next < 4096) { prefix[next] = (uint16_t)pref; nch[next] = (uint8_t)c; next++; }
            pref = c;
        }
    }
    w.write_bits((uint32_t)pref, 12);
    int bits = w.finish();
    delete[] prefix; delete[] nch;
    return 4 + bits;
}

static void lzw_emit(uint16_t* prefix, uint8_t* nch, int code, uint8_t* out, int* op) {
    uint8_t tmp[600]; int t = 0;
    while (code >= 256) { tmp[t++] = nch[code]; code = prefix[code]; }
    tmp[t++] = (uint8_t)code;
    while (t > 0) out[(*op)++] = tmp[--t];
}

int lzw_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    uint16_t* prefix = new uint16_t[4096];
    uint8_t*  nch    = new uint8_t[4096];
    int next = 256;
    BitSource r(in + 4, n - 4);
    int op = 0;
    int prev = (int)r.read_bits(12);
    if (prev >= 256) { delete[] prefix; delete[] nch; return -1; }
    if (op < cap) out[op++] = (uint8_t)prev;
    auto first_char = [&](int code) -> uint8_t {
        while (code >= 256) code = prefix[code];
        return (uint8_t)code;
    };
    while (op < orig) {
        int code = (int)r.read_bits(12);
        int entry; bool kw = false;
        if (code < next) entry = code; else { entry = prev; kw = true; }
        lzw_emit(prefix, nch, entry, out, &op);
        if (kw && op < cap) out[op++] = first_char(prev);
        if (next < 4096) { prefix[next] = (uint16_t)prev; nch[next] = first_char(entry); next++; }
        prev = code;
    }
    delete[] prefix; delete[] nch;
    return op;
}

#define LZ77_WIN 32768
#define LZ77_MIN 3
#define LZ77_MAX 258
static inline uint32_t lz_hash3(const uint8_t* p) {
    return ((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ p[2];
}

int lz77_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    const int H = 4096;
    int* head = new int[H];
    int* prev = new int[LZ77_WIN];
    bfill(head, 0xFF, (size_t)H * sizeof(int));
    BitSink w(out + 4, cap - 4);
    int i = 0;
    while (i < n) {
        int best_len = 0, best_off = 0;
        if (i + LZ77_MIN <= n) {
            uint32_t h = lz_hash3(in + i) & (H - 1);
            int cand = head[h];
            int depth = 0;
            while (cand >= 0 && depth < 64) {
                if (i - cand > LZ77_WIN) break;
                int l = 0;
                while (i + l < n && l < LZ77_MAX && in[cand + l] == in[i + l]) l++;
                if (l > best_len) { best_len = l; best_off = i - cand; if (l >= LZ77_MAX) break; }
                cand = prev[cand & (LZ77_WIN - 1)];
                depth++;
            }
        }
        if (best_len >= LZ77_MIN) {
            w.put_bit(1);
            w.write_bits((uint32_t)(best_off - 1), 15);
            w.write_bits((uint32_t)(best_len - LZ77_MIN), 8);
            for (int k = 0; k < best_len; k++, i++) {
                if (i + LZ77_MIN <= n) {
                    uint32_t h = lz_hash3(in + i) & (H - 1);
                    prev[i & (LZ77_WIN - 1)] = head[h];
                    head[h] = i;
                }
            }
        } else {
            w.put_bit(0);
            w.write_bits(in[i], 8);
            if (i + LZ77_MIN <= n) {
                uint32_t h = lz_hash3(in + i) & (H - 1);
                prev[i & (LZ77_WIN - 1)] = head[h];
                head[h] = i;
            }
            i++;
        }
    }
    int bits = w.finish();
    delete[] head; delete[] prev;
    return 4 + bits;
}

int lz77_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    BitSource r(in + 4, n - 4);
    int o = 0;
    for (int i = 0; i < orig; i++) {
        if (r.read_bit() == 0) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t)r.read_bits(8);
        } else {
            int off = (int)r.read_bits(15) + 1;
            int len = (int)r.read_bits(8) + LZ77_MIN;
            int src = o - off;
            if (src < 0) return -1;
            for (int k = 0; k < len; k++) { if (o >= cap) return -1; out[o++] = out[src + k]; }
            i += len - 1;
        }
    }
    return o;
}

int lzss_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    BitSink w(out + 4, cap - 4);
    int i = 0;
    while (i < n) {
        int best_len = 0, best_off = 0;
        int lo = i - 4096; if (lo < 0) lo = 0;
        for (int c = lo; c < i; c++) {
            int l = 0;
            while (i + l < n && l < 18 && in[c + l] == in[i + l]) l++;
            if (l > best_len) { best_len = l; best_off = i - c; }
        }
        if (best_len >= 3) {
            w.put_bit(1);
            w.write_bits((uint32_t)(best_off - 1), 12);
            w.write_bits((uint32_t)(best_len - 3), 4);
            i += best_len;
        } else {
            w.put_bit(0);
            w.write_bits(in[i], 8);
            i++;
        }
    }
    return 4 + w.finish();
}

int lzss_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    BitSource r(in + 4, n - 4);
    int o = 0;
    for (int i = 0; i < orig; i++) {
        if (r.read_bit() == 0) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t)r.read_bits(8);
        } else {
            int off = (int)r.read_bits(12) + 1;
            int len = (int)r.read_bits(4) + 3;
            int src = o - off;
            if (src < 0) return -1;
            for (int k = 0; k < len; k++) { if (o >= cap) return -1; out[o++] = out[src + k]; }
            i += len - 1;
        }
    }
    return o;
}

int lz78_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    const int D = 8192;
    uint16_t* dp = new uint16_t[D];
    uint8_t* dc = new uint8_t[D];
    int next = 256;
    BitSink w(out + 4, cap - 4);
    int i = 0;
    int cur = -1;
    while (i < n) {
        uint8_t c = in[i++];
        if (cur < 0) { cur = c; continue; }
        int found = -1;
        for (int code = 256; code < next; code++) {
            if (dp[code] == (uint16_t)cur && dc[code] == c) { found = code; break; }
        }
        if (found >= 0) {
            cur = found;
        } else {
            w.write_bits((uint32_t)cur, 16);
            w.write_bits(c, 8);
            if (next < D) { dp[next] = (uint16_t)cur; dc[next] = c; next++; }
            cur = -1;
        }
    }
    if (cur >= 0) { w.write_bits((uint32_t)cur, 16); w.write_bits(0, 8); }
    int bits = w.finish();
    delete[] dp; delete[] dc;
    return 4 + bits;
}

int lz78_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    const int D = 8192;
    uint16_t* dp = new uint16_t[D];
    uint8_t* dc = new uint8_t[D];
    int next = 256;
    BitSource r(in + 4, n - 4);
    int o = 0;
    uint8_t tmp[8192];
    while (o < orig) {
        int pidx = (int)r.read_bits(16);
        uint8_t c = (uint8_t)r.read_bits(8);
        int t = 0;
        if (pidx != 0xFFFF) {
            int x = pidx;
            while (x >= 256) { tmp[t++] = dc[x]; x = dp[x]; }
            tmp[t++] = (uint8_t)x;
            for (int a = 0, b = t - 1; a < b; a++, b--) { uint8_t tt = tmp[a]; tmp[a] = tmp[b]; tmp[b] = tt; }
        }
        tmp[t++] = c;
        for (int k = 0; k < t && o < orig; k++) { if (o >= cap) break; out[o++] = tmp[k]; }
        if (next < D) { dp[next] = (uint16_t)pidx; dc[next] = c; next++; }
    }
    delete[] dp; delete[] dc;
    return o;
}

int lzrw1_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    BitSink w(out + 4, cap - 4);
    int i = 0;
    while (i < n) {
        int best_len = 0, best_off = 0;
        int lo = i - 4096; if (lo < 0) lo = 0;
        for (int c = lo; c < i; c++) {
            int l = 0;
            while (i + l < n && l < 17 && in[c + l] == in[i + l]) l++;
            if (l > best_len) { best_len = l; best_off = i - c; }
        }
        if (best_len >= 3 && best_off <= 4096) {
            w.put_bit(1);
            w.write_bits((uint32_t)(best_off - 1), 12);
            w.write_bits((uint32_t)(best_len - 3), 4);
            i += best_len;
        } else {
            w.put_bit(0);
            w.write_bits(in[i], 8);
            i++;
        }
    }
    return 4 + w.finish();
}

int lzrw1_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    BitSource r(in + 4, n - 4);
    int o = 0;
    for (int i = 0; i < orig; i++) {
        if (r.read_bit() == 0) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t)r.read_bits(8);
        } else {
            int off = (int)r.read_bits(12) + 1;
            int len = (int)r.read_bits(4) + 3;
            int src = o - off; if (src < 0) return -1;
            for (int k = 0; k < len; k++) { if (o >= cap) return -1; out[o++] = out[src + k]; }
            i += len - 1;
        }
    }
    return o;
}

int lzp_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    const int H = 4096;
    int* pred = new int[H];
    bfill(pred, 0xFF, (size_t)H * sizeof(int));
    BitSink w(out + 4, cap - 4);
    for (int i = 0; i < n; i++) {
        uint32_t ctx = 0;
        if (i >= 2) ctx = ((uint32_t)in[i-2] << 8 | in[i-1]) & (H - 1);
        int p = pred[ctx];
        if (p >= 0 && p < i && in[p] == in[i]) { w.put_bit(1); }
        else { w.put_bit(0); w.write_bits(in[i], 8); }
        pred[ctx] = i;
    }
    delete[] pred;
    return 4 + w.finish();
}

int lzp_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    const int H = 4096;
    int* pred = new int[H];
    bfill(pred, 0xFF, (size_t)H * sizeof(int));
    BitSource r(in + 4, n - 4);
    for (int i = 0; i < orig; i++) {
        uint32_t ctx = 0;
        if (i >= 2) ctx = ((uint32_t)out[i-2] << 8 | out[i-1]) & (H - 1);
        int p = pred[ctx];
        if (r.read_bit() == 1) {
            if (p < 0 || p >= i) { delete[] pred; return -1; }
            out[i] = out[p];
        } else {
            if (i >= cap) { delete[] pred; return -1; }
            out[i] = (uint8_t)r.read_bits(8);
        }
        pred[ctx] = i;
    }
    delete[] pred;
    return orig;
}

namespace {
int g_fails = 0;
bool rt(const char*, int (*enc)(const uint8_t*, int, uint8_t*, int),
        int (*dec)(const uint8_t*, int, uint8_t*, int), const uint8_t* d, int n) {
    int cap = n * 4 + 4096;
    uint8_t* c = new uint8_t[cap > 0 ? cap : 1];
    uint8_t* dd = new uint8_t[n + 64];
    int cl = enc(d, n, c, cap);
    if (cl < 0) { g_fails++; delete[] c; delete[] dd; return false; }
    int dl = dec(c, cl, dd, n + 64);
    bool ok = (dl == n) && (n == 0 || bcmp(d, dd, (size_t)n) == 0);
    if (!ok) g_fails++;
    delete[] c; delete[] dd;
    return ok;
}
uint32_t seed = 0x3333;
uint32_t rnd() { seed = seed * 1664525u + 1013904223u; return seed; }
}

int lz_self_test() {
    g_fails = 0;
    const int N = 3000;
    uint8_t* d = new uint8_t[N];
    struct T { const char* nm; int (*enc)(const uint8_t*, int, uint8_t*, int); int (*dec)(const uint8_t*, int, uint8_t*, int); };
    T all[] = {
        {"lz77", lz77_encode, lz77_decode}, {"lzss", lzss_encode, lzss_decode},
        {"lz78", lz78_encode, lz78_decode}, {"lzw",  lzw_encode,  lzw_decode},
        {"lzrw1",lzrw1_encode,lzrw1_decode},{"lzp",  lzp_encode,  lzp_decode},
    };
    int M = (int)(sizeof(all)/sizeof(all[0]));
    {
        const char* t = "abracadabra abracadabra ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        for (int m = 0; m < M; m++) rt(all[m].nm, all[m].enc, all[m].dec, d, N);
    }
    for (int i = 0; i < N; i++) d[i] = (uint8_t)(rnd() & 0xFF);
    for (int m = 0; m < M; m++) rt(all[m].nm, all[m].enc, all[m].dec, d, N);
    bfill(d, 0, N);
    for (int m = 0; m < M; m++) rt(all[m].nm, all[m].enc, all[m].dec, d, N);
    for (int m = 0; m < M; m++) rt(all[m].nm, all[m].enc, all[m].dec, d, 0);
    d[0] = 7;
    for (int m = 0; m < M; m++) rt(all[m].nm, all[m].enc, all[m].dec, d, 1);
    {
        uint8_t big[4000];
        uint32_t s2 = 99;
        for (int i = 0; i < 4000; i++) { s2 = s2*1664525u+1013904223u; big[i]=(uint8_t)(s2&0xFF); }
        rt("lzw", lzw_encode, lzw_decode, big, 4000);
    }
    delete[] d;
    return g_fails;
}

} // namespace compress
} // namespace nefu