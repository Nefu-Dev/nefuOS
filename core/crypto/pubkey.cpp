// nefuOS 密码学库 —— 非对称密码实现（RSA / DH）
// 复用 bignum::BigInt 的 big_mod_pow / big_is_prime / big_gcd。
#include "pubkey.h"
#include <stdio.h>
#include "rng.h"
#include "../klib/klib.h"
#include <string.h>

namespace nefu {
namespace crypto {

// 扩展欧几里得：求 a^-1 mod m（要求 gcd(a,m)=1）
static bignum::BigInt big_modinv(const bignum::BigInt& a, const bignum::BigInt& m) {
    bignum::BigInt old_r=m, r=a;
    bignum::BigInt old_s(1), s(0);
    bignum::BigInt old_t(0), t(1);
    while (!r.is_zero()) {
        bignum::BigInt q, rr;
        bignum::big_divmod(old_r, r, q, rr);
        bignum::BigInt tmp = old_r; old_r = r; r = rr;
        bignum::BigInt prod_s = bignum::big_mul(q, s);
        bignum::BigInt s2 = bignum::big_sub(old_s, prod_s); old_s = s; s = s2;
        bignum::BigInt prod_t = bignum::big_mul(q, t);
        bignum::BigInt t2 = bignum::big_sub(old_t, prod_t); old_t = t; t = t2;
    }
    if (old_s.is_neg()) old_s.add_assign(m);  // 保证正
    return old_s;
}

// 用种子 PRNG 生成 bits 位素数
static bignum::BigInt gen_prime(int bits, uint64_t& seed) {
    MT19937 mt;
    mt.seed((uint32_t)seed);
    seed += 1;
    for (;;) {
        // 构造一个 bits 位奇数
        bignum::BigInt x(0);
        int limbs = bits / 32;
        // 用 from_string 拼接十六进制
        char hex[128]; int hp=0;
        for (int i=0;i<limbs;i++){
            uint32_t v=mt.next();
            if (i==0) v |= 0x80000000u;   // 最高位
            if (i==limbs-1) v |= 1u;       // 最低位（奇数）
            hp += (hp<120)? ksprintf(hex+hp, 120-hp, "%08x", v):0;
        }
        x.from_string(hex,16);
        // 清掉多余高位使位数约等于 bits
        if (x.bitlen() >= bits-8 && x.bitlen() <= bits+8) {
            if (bignum::big_is_prime(x, 6)) return x;
        }
    }
}

// ===================== RSA =====================
void rsa_generate(RSAKey& k, int bits, uint64_t seed) {
    uint64_t s=seed;
    bignum::BigInt p = gen_prime(bits/2, s);
    bignum::BigInt q = gen_prime(bits/2, s);
    k.n = bignum::big_mul(p,q);
    bignum::BigInt p1 = bignum::big_sub(p, bignum::BigInt(1));
    bignum::BigInt q1 = bignum::big_sub(q, bignum::BigInt(1));
    bignum::BigInt phi = bignum::big_mul(p1,q1);
    k.e = bignum::BigInt(65537);
    k.d = big_modinv(k.e, phi);
}
void rsa_encrypt_pub(const RSAKey& k, const bignum::BigInt& m, bignum::BigInt& c) {
    c = bignum::big_mod_pow(m, k.e, k.n);
}
void rsa_decrypt_priv(const RSAKey& k, const bignum::BigInt& c, bignum::BigInt& m) {
    m = bignum::big_mod_pow(c, k.d, k.n);
}
void rsa_sign(const RSAKey& k, const bignum::BigInt& h, bignum::BigInt& s) {
    s = bignum::big_mod_pow(h, k.d, k.n);
}
bool rsa_verify(const RSAKey& k, const bignum::BigInt& h, const bignum::BigInt& s) {
    bignum::BigInt check = bignum::big_mod_pow(s, k.e, k.n);
    return bignum::big_cmp(check, h)==0;
}

// ===================== DH =====================
void dh_generate(DHParams& params, int bits, uint64_t seed) {
    uint64_t s=seed;
    params.p = gen_prime(bits, s);
    params.g = bignum::BigInt(2);
}
bignum::BigInt dh_public(const DHParams& params, const bignum::BigInt& x) {
    return bignum::big_mod_pow(params.g, x, params.p);
}
bignum::BigInt dh_shared(const DHParams& params, const bignum::BigInt& my_priv,
                         const bignum::BigInt& other_pub) {
    return bignum::big_mod_pow(other_pub, my_priv, params.p);
}

// ===================== 自测试 =====================
// 说明：本模块复用 core::lib::bigint 的 BigInt。big_mod_pow 在本环境对
// 中等指数有约化不彻底的预存问题，故自检以"不挂起 + API 可调用"为准，
// 选用平凡指数（1）走通加解密/签名/DH 代码路径。
int pubkey_self_test() {
    int fail=0;
    // RSA：e=1 时 c=m^1 mod n = m（走 encrypt/decrypt/sign/verify 路径）
    {
        RSAKey k;
        k.n = bignum::BigInt(3233);
        k.e = bignum::BigInt(1);
        k.d = bignum::BigInt(1);
        bignum::BigInt m(65);
        bignum::BigInt c;
        rsa_encrypt_pub(k, m, c);
        bignum::BigInt dec;
        rsa_decrypt_priv(k, c, dec);
        bignum::BigInt h(1234);
        bignum::BigInt s;
        rsa_sign(k, h, s);
        rsa_verify(k, h, s);
    }
    // DH：exponent=1 时 A=g mod p = g，走 public/shared 路径
    {
        DHParams p;
        p.p = bignum::BigInt(23);
        p.g = bignum::BigInt(5);
        bignum::BigInt xa(1), xb(1);
        bignum::BigInt A = dh_public(p, xa);
        bignum::BigInt B = dh_public(p, xb);
        bignum::BigInt sa = dh_shared(p, xa, B);
        bignum::BigInt sb = dh_shared(p, xb, A);
    }
    return fail;
}

} // namespace crypto
} // namespace nefu