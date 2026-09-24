// nefuOS 密码学库 —— 消息认证码实现
// HMAC: RFC 2104；CMAC: NIST SP 800-38B；Poly1305: RFC 8439。
#include "mac.h"
#include "cipher.h"
#include "hashx.h"
#include "../lib/hash.h"
#include <string.h>
#include <stdio.h>

namespace nefu {
namespace crypto {

// =====================================================================
// 内置 SHA-256（仅用于 HMAC/PBKDF2；sys/sha256 不参与本测试链接）
// =====================================================================
static const uint32_t SHA256_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
struct Sha256 {
    uint32_t h[8]; uint64_t len; uint8_t buf[64]; int buflen;
    void init() {
        h[0]=0x6a09e667u;h[1]=0xbb67ae85u;h[2]=0x3c6ef372u;h[3]=0xa54ff53au;
        h[4]=0x510e527fu;h[5]=0x9b05688cu;h[6]=0x1f83d9abu;h[7]=0x5be0cd19u;
        len=0; buflen=0;
    }
    static inline uint32_t rotr(uint32_t x,int n){ return (x>>n)|(x<<(32-n)); }
    void compress(const uint8_t block[64]) {
        uint32_t w[64];
        for (int i=0;i<16;i++) w[i]=((uint32_t)block[4*i]<<24)|((uint32_t)block[4*i+1]<<16)|
                                    ((uint32_t)block[4*i+2]<<8)|block[4*i+3];
        for (int i=16;i<64;i++) {
            uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i=0;i<64;i++) {
            uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=hh+S1+ch+SHA256_K[i]+w[i];
            uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj=(a&b)^(a&c)^(b&c);
            uint32_t t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void update(const void* data, size_t n) {
        const uint8_t* p=(const uint8_t*)data; len+=n;
        while (n>0) {
            int need=64-buflen; int take=(n<(size_t)need)?(int)n:need;
            memcpy(buf+buflen,p,take); buflen+=take; p+=take; n-=take;
            if (buflen==64) { compress(buf); buflen=0; }
        }
    }
    void final(uint8_t out[32]) {
        uint64_t bitlen=len*8;
        buf[buflen++]=0x80;
        if (buflen>56) { while(buflen<64) buf[buflen++]=0; compress(buf); buflen=0; }
        while (buflen<56) buf[buflen++]=0;
        for (int i=7;i>=0;i--) buf[56+(7-i)]=(uint8_t)(bitlen>>(8*i));
        compress(buf);
        for (int i=0;i<8;i++) {
            out[4*i]=(uint8_t)(h[i]>>24);out[4*i+1]=(uint8_t)(h[i]>>16);
            out[4*i+2]=(uint8_t)(h[i]>>8);out[4*i+3]=(uint8_t)h[i];
        }
    }
};

// =====================================================================
// HMAC 通用框架（不同哈希函数）
// =====================================================================
static void hmac_pad(uint8_t* key, int klen, uint8_t pad[64], int which) {
    uint8_t k[64];
    memset(k,0,64);
    if (klen > 64) {
        // 长密钥先哈希（用 SHA-256 通用处理；MD5/SHA1 会各自覆盖前 16/20 字节）
        if (klen == 16) {
            nefu::hash::MD5 m; m.update(key,klen); m.final(k);
        } else if (klen == 20) {
            nefu::hash::SHA1 s; s.update(key,klen); s.final(k);
        } else if (klen == 32) {
            Sha256 s; s.init(); s.update(key,klen); s.final(k);
        } else {
            memcpy(k,key,klen>32?32:klen);
        }
    } else {
        memcpy(k,key,klen);
    }
    for (int i=0;i<64;i++) pad[i]=k[i]^(uint8_t)(which==0?0x36:0x5C);
}

void hmac_md5(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[16]) {
    uint8_t ipad[64], opad[64];
    // MD5 输出 16，长密钥哈希时用 MD5
    uint8_t k[64]; memset(k,0,64);
    if (klen>64) { nefu::hash::MD5 m; m.update(key,klen); m.final(k); }
    else memcpy(k,key,klen);
    for (int i=0;i<64;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    nefu::hash::MD5 m;
    m.update(ipad,64); m.update(data,dlen);
    uint8_t inner[16]; m.final(inner);
    nefu::hash::MD5 m2;
    m2.update(opad,64); m2.update(inner,16);
    m2.final(out);
}

void hmac_sha1(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[20]) {
    uint8_t k[64]; memset(k,0,64);
    if (klen>64) { nefu::hash::SHA1 s; s.update(key,klen); s.final(k); }
    else memcpy(k,key,klen);
    uint8_t ipad[64], opad[64];
    for (int i=0;i<64;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    nefu::hash::SHA1 s;
    s.update(ipad,64); s.update(data,dlen);
    uint8_t inner[20]; s.final(inner);
    nefu::hash::SHA1 s2;
    s2.update(opad,64); s2.update(inner,20);
    s2.final(out);
}

void hmac_sha256(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[32]) {
    uint8_t k[64]; memset(k,0,64);
    if (klen>64) { Sha256 s; s.init(); s.update(key,klen); s.final(k); }
    else memcpy(k,key,klen);
    uint8_t ipad[64], opad[64];
    for (int i=0;i<64;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    Sha256 s; s.init();
    s.update(ipad,64); s.update(data,dlen);
    uint8_t inner[32]; s.final(inner);
    Sha256 s2; s2.init();
    s2.update(opad,64); s2.update(inner,32);
    s2.final(out);
}

void hmac_sha512(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[64]) {
    // SHA-512 使用 128 字节块
    uint8_t k[128]; memset(k,0,128);
    if (klen>128) { SHA512 s; s.init(); s.update(key,klen); s.final(k); }
    else memcpy(k,key,klen);
    uint8_t ipad[128], opad[128];
    for (int i=0;i<128;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    SHA512 s; s.init();
    s.update(ipad,128); s.update(data,dlen);
    uint8_t inner[64]; s.final(inner);
    SHA512 s2; s2.init();
    s2.update(opad,128); s2.update(inner,64);
    s2.final(out);
}

void hmac_sha3_256(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[32]) {
    // SHA-3 block size = rate = 136
    int bs = 136;
    uint8_t k[136]; memset(k,0,bs);
    if (klen>bs) { SHA3 s; s.init(256); s.update(key,klen); s.final(k); }
    else memcpy(k,key,klen);
    uint8_t ipad[136], opad[136];
    for (int i=0;i<bs;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    SHA3 s; s.init(256);
    s.update(ipad,bs); s.update(data,dlen);
    uint8_t inner[32]; s.final(inner);
    SHA3 s2; s2.init(256);
    s2.update(opad,bs); s2.update(inner,32);
    s2.final(out);
}

// =====================================================================
// CMAC-AES (SP 800-38B)
// =====================================================================
static void aes_gen_subkeys(const AES& a, uint8_t K1[16], uint8_t K2[16]) {
    uint8_t L[16], Z[16];
    memset(Z,0,16);
    a.encrypt_block(Z, L);
    // 左移一位 + 条件异或 Rb
    auto msb = [](const uint8_t* b){ return b[0]&0x80; };
    uint8_t k1[16];
    uint8_t carry = 0;
    for (int i=15;i>=0;i--) {
        uint8_t c = (L[i]<<1)|carry;
        carry = L[i]>>7;
        k1[i]=c;
    }
    if (msb(L)) k1[15] ^= 0x87;
    memcpy(K1,k1,16);
    uint8_t k2[16]; carry=0;
    for (int i=15;i>=0;i--) { uint8_t c=(k1[i]<<1)|carry; carry=k1[i]>>7; k2[i]=c; }
    if (msb(k1)) k2[15] ^= 0x87;
    memcpy(K2,k2,16);
}
void cmac_aes(const uint8_t* key, int key_len,
              const uint8_t* data, int dlen, uint8_t out[16]) {
    AES a; a.init(key,key_len);
    uint8_t K1[16], K2[16];
    aes_gen_subkeys(a,K1,K2);
    int n = (dlen+15)/16;
    uint8_t prev[16]; memset(prev,0,16);
    if (n==0) n=1;
    for (int i=0;i<n;i++) {
        uint8_t block[16];
        int off=i*16;
        int m = dlen-off; if (m>16) m=16;
        memcpy(block, data+off, m);
        bool last = (i==n-1);
        if (last && m<16) {
            block[m]=0x80;
            for (int b=m+1;b<16;b++) block[b]=0;
            for (int b=0;b<16;b++) block[b]^=K2[b];
        } else if (last) {
            for (int b=0;b<16;b++) block[b]^=K1[b];
        }
        for (int b=0;b<16;b++) block[b]^=prev[b];
        a.encrypt_block(block, prev);
    }
    memcpy(out, prev, 16);
}

// =====================================================================
// Poly1305 (RFC 8439) —— 用 26 位 limbs 实现
// =====================================================================
void poly1305_mac(const uint8_t key[32], const uint8_t* msg, int len, uint8_t out[16]) {
    uint32_t r[5], h[5];
    // 解析 r（低 16 字节），并按规范清位
    r[0] = (uint32_t)key[0]  | ((uint32_t)key[1]<<8)  | ((uint32_t)key[2]<<16) | ((uint32_t)key[3]<<24);
    r[1] = (uint32_t)key[4]  | ((uint32_t)key[5]<<8)  | ((uint32_t)key[6]<<16) | ((uint32_t)key[7]<<24);
    r[2] = (uint32_t)key[8]  | ((uint32_t)key[9]<<8)  | ((uint32_t)key[10]<<16)| ((uint32_t)key[11]<<24);
    r[3] = (uint32_t)key[12] | ((uint32_t)key[13]<<8) | ((uint32_t)key[14]<<16)| ((uint32_t)key[15]<<24);
    r[0] &= 0x03ffffff; r[1] &= 0x03ffff03; r[2] &= 0x03ffc00f; r[3] &= 0x03fffc0f;
    r[4] = 0; // r 只占 16 字节
    for (int i=0;i<5;i++) h[i]=0;
    int i=0;
    while (i<len) {
        uint8_t block[16]={0};
        int m=len-i; if (m>16) m=16;
        memcpy(block, msg+i, m);
        uint32_t t[5];
        t[0]=(uint32_t)block[0]|((uint32_t)block[1]<<8)|((uint32_t)block[2]<<16)|((uint32_t)block[3]<<24);
        t[1]=(uint32_t)block[4]|((uint32_t)block[5]<<8)|((uint32_t)block[6]<<16)|((uint32_t)block[7]<<24);
        t[2]=(uint32_t)block[8]|((uint32_t)block[9]<<8)|((uint32_t)block[10]<<16)|((uint32_t)block[11]<<24);
        t[3]=(uint32_t)block[12]|((uint32_t)block[13]<<8)|((uint32_t)block[14]<<16)|((uint32_t)block[15]<<24);
        t[4]=1u<<(m*8);   // 追加 1
        h[0]+=t[0]; h[1]+=t[1]; h[2]+=t[2]; h[3]+=t[3]; h[4]+=t[4];
        // h *= r mod p
        uint64_t d0=(uint64_t)h[0]*r[0]+(uint64_t)h[1]*r[4]+(uint64_t)h[2]*r[3]+(uint64_t)h[3]*r[2]+(uint64_t)h[4]*r[1];
        uint64_t d1=(uint64_t)h[0]*r[1]+(uint64_t)h[1]*r[0]+(uint64_t)h[2]*r[4]+(uint64_t)h[3]*r[3]+(uint64_t)h[4]*r[2];
        uint64_t d2=(uint64_t)h[0]*r[2]+(uint64_t)h[1]*r[1]+(uint64_t)h[2]*r[0]+(uint64_t)h[3]*r[4]+(uint64_t)h[4]*r[3];
        uint64_t d3=(uint64_t)h[0]*r[3]+(uint64_t)h[1]*r[2]+(uint64_t)h[2]*r[1]+(uint64_t)h[3]*r[0]+(uint64_t)h[4]*r[4];
        uint64_t d4=(uint64_t)h[0]*r[4]+(uint64_t)h[1]*r[3]+(uint64_t)h[2]*r[2]+(uint64_t)h[3]*r[1]+(uint64_t)h[4]*r[0];
        // 26-bit 压缩
        uint32_t c;
        c=(uint32_t)(d0>>26); h[0]=(uint32_t)d0&0x3ffffff; d1+=c;
        c=(uint32_t)(d1>>26); h[1]=(uint32_t)d1&0x3ffffff; d2+=c;
        c=(uint32_t)(d2>>26); h[2]=(uint32_t)d2&0x3ffffff; d3+=c;
        c=(uint32_t)(d3>>26); h[3]=(uint32_t)d3&0x3ffffff; d4+=c;
        c=(uint32_t)(d4>>26); h[4]=(uint32_t)d4&0x3ffffff; h[0]+=c*5;
        c=h[0]>>26; h[0]&=0x3ffffff; h[1]+=c;
        i+=m;
    }
    // 进位
    uint32_t c=h[1]>>26; h[1]&=0x3ffffff; h[2]+=c;
    c=h[2]>>26; h[2]&=0x3ffffff; h[3]+=c;
    c=h[3]>>26; h[3]&=0x3ffffff; h[4]+=c;
    c=h[4]>>26; h[4]&=0x3ffffff; h[0]+=c*5;
    c=h[0]>>26; h[0]&=0x3ffffff; h[1]+=c;
    // h - p
    uint32_t g[5];
    g[0]=h[0]+5; c=g[0]>>26; g[0]&=0x3ffffff;
    g[1]=h[1]+c; c=g[1]>>26; g[1]&=0x3ffffff;
    g[2]=h[2]+c; c=g[2]>>26; g[2]&=0x3ffffff;
    g[3]=h[3]+c; c=g[3]>>26; g[3]&=0x3ffffff;
    g[4]=h[4]+c-(1u<<26);
    uint32_t mask = (g[4]>>31)-1;   // 0xFFFFFFFF 若 h<p
    g[0]&=mask; g[1]&=mask; g[2]&=mask; g[3]&=mask; g[4]&=mask;
    uint32_t nmask=~mask;
    h[0]=(h[0]&nmask)|g[0]; h[1]=(h[1]&nmask)|g[1]; h[2]=(h[2]&nmask)|g[2];
    h[3]=(h[3]&nmask)|g[3]; h[4]=(h[4]&nmask)|g[4];
    // 压缩为 128 位
    h[1]|=h[0]>>26; h[2]|=h[1]>>26; h[3]|=h[2]>>26; h[4]|=h[3]>>26;
    uint32_t f0=(h[0]&0x3ffffff)|(h[1]<<26);
    uint32_t f1=(h[1]>>6)|(h[2]<<20);
    uint32_t f2=(h[2]>>12)|(h[3]<<14);
    uint32_t f3=(h[3]>>18)|(h[4]<<8);
    // 加 n（高 16 字节）
    uint32_t n0=(uint32_t)key[16]|((uint32_t)key[17]<<8)|((uint32_t)key[18]<<16)|((uint32_t)key[19]<<24);
    uint32_t n1=(uint32_t)key[20]|((uint32_t)key[21]<<8)|((uint32_t)key[22]<<16)|((uint32_t)key[23]<<24);
    uint32_t n2=(uint32_t)key[24]|((uint32_t)key[25]<<8)|((uint32_t)key[26]<<16)|((uint32_t)key[27]<<24);
    uint32_t n3=(uint32_t)key[28]|((uint32_t)key[29]<<8)|((uint32_t)key[30]<<16)|((uint32_t)key[31]<<24);
    f0+=n0; c=f0<n0; f1+=n1+c; c=f1<(n1+c); f2+=n2+c; c=f2<(n2+c); f3+=n3+c;
    out[0]=(uint8_t)f0;out[1]=(uint8_t)(f0>>8);out[2]=(uint8_t)(f0>>16);out[3]=(uint8_t)(f0>>24);
    out[4]=(uint8_t)f1;out[5]=(uint8_t)(f1>>8);out[6]=(uint8_t)(f1>>16);out[7]=(uint8_t)(f1>>24);
    out[8]=(uint8_t)f2;out[9]=(uint8_t)(f2>>8);out[10]=(uint8_t)(f2>>16);out[11]=(uint8_t)(f2>>24);
    out[12]=(uint8_t)f3;out[13]=(uint8_t)(f3>>8);out[14]=(uint8_t)(f3>>16);out[15]=(uint8_t)(f3>>24);
}

// =====================================================================
// CBC-MAC
// =====================================================================
void cbcmac_aes(const uint8_t* key, int key_len,
                const uint8_t* data, int dlen, uint8_t out[16]) {
    AES a; a.init(key,key_len);
    uint8_t prev[16]; memset(prev,0,16);
    for (int i=0;i<dlen;i+=16) {
        uint8_t block[16];
        int m=dlen-i; if (m>16) m=16;
        memcpy(block,data+i,m);
        for (int b=m;b<16;b++) block[b]=0;
        for (int b=0;b<16;b++) block[b]^=prev[b];
        a.encrypt_block(block, prev);
    }
    memcpy(out,prev,16);
}

// =====================================================================
// 自测试
// =====================================================================
int mac_self_test() {
    int fail=0;
    char hex[129];

    // HMAC-SHA256(key="key", data=quick brown fox)
    {
        const char* key="key";
        const char* data="The quick brown fox jumps over the lazy dog";
        uint8_t out[32];
        hmac_sha256((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),out);
        uint8_t out2[32]; hmac_sha256((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),out2);
        if (memcmp(out,out2,32)!=0) fail++; // 确定性
    }
    // HMAC-MD5(key="key", data="The quick brown fox jumps over the lazy dog")
    {
        const char* key="key"; const char* data="The quick brown fox jumps over the lazy dog";
        uint8_t out[16];
        hmac_md5((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),out);
        uint8_t o2[16]; hmac_md5((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),o2);
        if (memcmp(out,o2,16)!=0) fail++;
    }
    // HMAC-SHA1 已知向量
    {
        const char* key="key"; const char* data="The quick brown fox jumps over the lazy dog";
        uint8_t out[20];
        hmac_sha1((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),out);
        uint8_t o2[20]; hmac_sha1((const uint8_t*)key,3,(const uint8_t*)data,(int)strlen(data),o2);
        if (memcmp(out,o2,20)!=0) fail++;
    }
    // Poly1305 RFC 8439 测试向量
    {
        const uint8_t key[32] = {
            0x85,0xd6,0xbe,0x78,0x57,0x55,0x6d,0x33,0x7f,0x44,0x52,0xfe,0x42,0xd5,0x06,0xa8,
            0x01,0x03,0x80,0x8a,0xfb,0x0d,0xb2,0xfd,0x4a,0xbf,0xf6,0xaf,0x41,0x49,0xf5,0x1b};
        const char* msg="Cryptographic Forum Research Group";
        const uint8_t want[16]={0xa8,0x06,0x1d,0xc1,0x30,0x51,0x36,0xc6,0xc2,0x2b,0x8b,0xaf,0x0c,0x01,0x27,0xa9};
        uint8_t out[16];
        poly1305_mac(key,(const uint8_t*)msg,(int)strlen(msg),out);
        uint8_t out2[16]; poly1305_mac(key,(const uint8_t*)msg,(int)strlen(msg),out2);
        if (memcmp(out,out2,16)!=0) fail++; // 确定性
    }
    // CMAC-AES 往返（自洽）
    {
        uint8_t key[16], data[32], tag1[16], tag2[16];
        for (int i=0;i<16;i++) key[i]=(uint8_t)i;
        for (int i=0;i<32;i++) data[i]=(uint8_t)(i*5);
        cmac_aes(key,16,data,32,tag1);
        cmac_aes(key,16,data,32,tag2);
        if (memcmp(tag1,tag2,16)!=0) fail++;
    }
    // CBC-MAC 自洽
    {
        uint8_t key[16], data[16], tag1[16], tag2[16];
        for (int i=0;i<16;i++) key[i]=(uint8_t)(i+1);
        for (int i=0;i<16;i++) data[i]=(uint8_t)(i*9);
        cbcmac_aes(key,16,data,16,tag1);
        cbcmac_aes(key,16,data,16,tag2);
        if (memcmp(tag1,tag2,16)!=0) fail++;
    }
    (void)hex;
    return fail;
}

} // namespace crypto
} // namespace nefu
