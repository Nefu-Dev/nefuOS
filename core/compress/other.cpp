// nefuOS 压缩算法库 —— 整数编码与差分编码实现
#include "other.h"
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

// =====================================================================
// Delta 编码 / DPCM：out[i] = in[i] - in[i-1]（模 256）
// =====================================================================
int delta_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < n + 4) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    uint8_t prev = 0;
    for (int i = 0; i < n; i++) {
        out[4 + i] = (uint8_t)(in[i] - prev);
        prev = in[i];
    }
    return 4 + n;
}

int delta_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    uint8_t prev = 0;
    for (int i = 0; i < orig; i++) {
        out[i] = (uint8_t)(in[4 + i] + prev);
        prev = out[i];
    }
    return orig;
}

// =====================================================================
// varint (LEB128 无符号)：每字节低 7 位数据，高位=1 表示还有后续
// =====================================================================
int varint_encode(const uint32_t* in, int n, uint8_t* out, int cap) {
    if (cap < n * 5 + 4) return -1;
    put_u32_le(out, (uint32_t)n);
    int pos = 4;
    for (int i = 0; i < n; i++) {
        uint32_t v = in[i];
        while (v >= 0x80) {
            out[pos++] = (uint8_t)(v | 0x80);
            v >>= 7;
        }
        out[pos++] = (uint8_t)v;
    }
    return pos;
}

int varint_decode(const uint8_t* in, int n, uint32_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    int p = 4;
    for (int i = 0; i < orig; i++) {
        uint32_t v = 0; int shift = 0; uint8_t b;
        do {
            b = in[p++];
            v |= (uint32_t)(b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
        out[i] = v;
    }
    return orig;
}

// =====================================================================
// Elias gamma：对正整数 x，设 N = floor(log2 x)，编码为 N 个 0 后跟 N+1 位 x 的二进制
// =====================================================================
namespace {
void put_gamma(BitSink& bs, uint32_t x) {
    // x >= 1
    int n = 0; uint32_t t = x;
    while (t >>= 1) n++;
    for (int i = 0; i < n; i++) bs.put_bit(0);
    for (int i = n; i >= 0; i--) bs.put_bit((x >> i) & 1);
}
uint32_t get_gamma(BitSource& bs) {
    int n = 0;
    while (!bs.read_bit()) n++;
    uint32_t x = 1;
    for (int i = 0; i < n; i++) { x = (x << 1) | bs.read_bit(); }
    return x;
}
}

int elias_gamma_encode(const uint32_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4 + n * 11) return -1;
    put_u32_le(out, (uint32_t)n);
    BitSink bs(out + 4, cap - 4);
    for (int i = 0; i < n; i++) put_gamma(bs, in[i] + 1);
    return 4 + bs.finish();
}

int elias_gamma_decode(const uint8_t* in, int n, uint32_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    BitSource bs(in + 4, n - 4);
    for (int i = 0; i < orig; i++) out[i] = get_gamma(bs) - 1;
    return orig;
}

// =====================================================================
// Elias delta：先对 N=floor(log2 x) 做 gamma，再写 x 低 N 位
// =====================================================================
namespace {
void put_delta(BitSink& bs, uint32_t x) {
    int n = 0; uint32_t t = x;
    while (t >>= 1) n++;
    put_gamma(bs, (uint32_t)n + 1);
    for (int i = n - 1; i >= 0; i--) bs.put_bit((x >> i) & 1);
}
uint32_t get_delta(BitSource& bs) {
    uint32_t n = get_gamma(bs) - 1;
    uint32_t x = 1u << n;
    for (int i = n - 1; i >= 0; i--) x |= (uint32_t)bs.read_bit() << i;
    return x;
}
// Rice：商 q 一元编码，余数 r k 位
void put_rice(BitSink& bs, uint32_t x, int k) {
    uint32_t q = x >> k;
    for (uint32_t i = 0; i < q; i++) bs.put_bit(1);
    bs.put_bit(0);
    uint32_t r = x & ((1u << k) - 1);
    for (int i = k - 1; i >= 0; i--) bs.put_bit((r >> i) & 1);
}
uint32_t get_rice(BitSource& bs, int k) {
    uint32_t q = 0;
    while (bs.read_bit()) q++;
    uint32_t r = 0;
    for (int i = k - 1; i >= 0; i--) r |= (uint32_t)bs.read_bit() << i;
    return (q << k) | r;
}
// Fibonacci：Zeckendorf 表示 + 终止符 1
void put_fib(BitSink& bs, uint32_t x) {
    uint32_t fib[48]; fib[0]=1; fib[1]=2; int cnt=2;
    while (fib[cnt-1]+fib[cnt-2] <= x) { fib[cnt]=fib[cnt-1]+fib[cnt-2]; cnt++; }
    bool bits[48]; for (int i=0;i<cnt;i++) bits[i]=false;
    uint32_t rem=x;
    for (int i=cnt-1;i>=0;i--) if (fib[i]<=rem){bits[i]=true;rem-=fib[i];}
    for (int i=0;i<cnt;i++) bs.put_bit(bits[i]?1:0);
    bs.put_bit(1);
}
uint32_t get_fib(BitSource& bs) {
    uint32_t fib[48]; fib[0]=1; fib[1]=2;
    uint32_t sum=0; int prev=0;
    for (int i=0;;i++) {
        int b=bs.read_bit();
        if (prev==1 && b==1) break;
        if (b) { if(i==0)sum+=1; else if(i==1)sum+=2; else sum+=fib[i-1]; }
        prev=b;
        if (i>=2) { fib[i+1]=fib[i]+fib[i-1]; }
    }
    return sum;
}
}
int elias_delta_encode(const uint32_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4 + n * 20) return -1;
    put_u32_le(out, (uint32_t)n);
    BitSink bs(out + 4, cap - 4);
    for (int i = 0; i < n; i++) put_delta(bs, in[i] + 1);
    return 4 + bs.finish();
}
int elias_delta_decode(const uint8_t* in, int n, uint32_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    BitSource bs(in + 4, n - 4);
    for (int i = 0; i < orig; i++) out[i] = get_delta(bs) - 1;
    return orig;
}
int dpcm_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < n + 4) return -1;
    put_u32_le(out, (uint32_t)n);
    uint8_t prev = 128;
    for (int i = 0; i < n; i++) {
        int8_t d = (int8_t)((int)in[i] - prev);
        out[4 + i] = (uint8_t)d;
        prev = in[i];
    }
    return 4 + n;
}
int dpcm_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    uint8_t prev = 128;
    for (int i = 0; i < orig; i++) {
        prev = (uint8_t)(prev + (int8_t)in[4 + i]);
        out[i] = prev;
    }
    return orig;
}
// =====================================================================
// 自测
// =====================================================================
namespace { int g_fails = 0; }

int other_self_test() {
    g_fails = 0;
    const int N = 2000;
    uint8_t* d = new uint8_t[N];
    uint8_t* c = new uint8_t[N * 25];
    uint8_t* o = new uint8_t[N];
    uint32_t* vi = new uint32_t[N];
    uint32_t* vo = new uint32_t[N];

    // delta
    {
        const char* t = "abcdeeeeeeeeeeeeeee"; int tl=19; int k=0;
        for(int i=0;i<N;i++) d[i]=(uint8_t)t[k++%tl];
        int cl=delta_encode(d,N,c,N*25);
        int dl=delta_decode(c,cl,o,N);
        if(dl!=N || bcmp(d,o,N)!=0) g_fails++;
    }
    // varint
    {
        uint32_t seed=1;
        for(int i=0;i<N;i++){seed=seed*1664525u+1013904223u; vi[i]=seed&0x3FF;}
        int cl=varint_encode(vi,N,c,N*25);
        int dl=varint_decode(c,cl,vo,N);
        bool ok=(dl==N);
        if(ok) for(int i=0;i<N;i++) if(vi[i]!=vo[i]){ok=false;break;}
        if(!ok) g_fails++;
    }
    // elias gamma
    {
        uint32_t seed=2;
        for(int i=0;i<N;i++){seed=seed*1664525u+1013904223u; vi[i]=seed&0x3FF;}
        int cl=elias_gamma_encode(vi,N,c,N*25);
        int dl=elias_gamma_decode(c,cl,vo,N);
        bool ok=(dl==N);
        if(ok) for(int i=0;i<N;i++) if(vi[i]!=vo[i]){ok=false;break;}
        if(!ok) g_fails++;
    }
    // elias delta
    {
        uint32_t seed=3;
        for(int i=0;i<N;i++){seed=seed*1664525u+1013904223u; vi[i]=seed&0x3FF;}
        int cl=elias_delta_encode(vi,N,c,N*25);
        int dl=elias_delta_decode(c,cl,vo,N);
        bool ok=(dl==N);
        if(ok) for(int i=0;i<N;i++) if(vi[i]!=vo[i]){ok=false;break;}
        if(!ok) g_fails++;
    }
    // dpcm
    {
        uint32_t seed=4;
        for(int i=0;i<N;i++){seed=seed*1664525u+1013904223u; d[i]=(uint8_t)(128+(seed%200)-100);}
        int cl=dpcm_encode(d,N,c,N*25);
        int dl=dpcm_decode(c,cl,o,N);
        if(dl!=N || bcmp(d,o,N)!=0) g_fails++;
    }
    // 边界
    {
        int cl=delta_encode(d,0,c,N*25);
        int dl=delta_decode(c,cl,o,N);
        if(dl!=0) g_fails++;
    }
    delete[] d; delete[] c; delete[] o; delete[] vi; delete[] vo;
    return g_fails;
}

} // namespace compress
} // namespace nefu
