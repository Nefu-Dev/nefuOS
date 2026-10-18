// nefuOS native TLS 1.3 client (RFC 8446) - implementation
// X25519 (RFC 7748) + HKDF-SHA256 + AES-128-GCM / ChaCha20-Poly1305
// Chrome-style ClientHello fingerprint. No certificate validation (NoVerify).
#include "tls13.h"
#include "../crypto/cipher.h"
#include "../crypto/mac.h"
#include "../sys/sha256.h"
#include "../crypto/rng.h"
#include "../klib/klib.h"
#include <string.h>
#include <stdio.h>

namespace nefu {
namespace tls {

// ============================================================
// ============================================================
// ============================================================
// ============================================================
// ============================================================
// X25519 field arithmetic mod p = 2^255 - 19  (u64[4] limbs, LE)
// value always kept reduced mod p (0 <= v < p) after every op.
// ============================================================
struct Fe {
    uint64_t v[4];
};

static const uint64_t FE_P0 = 0xFFFFFFFFFFFFFFEDull;

static void fe_copy(Fe& d, const Fe& s) { for (int i = 0; i < 4; i++) d.v[i] = s.v[i]; }

static void fe_cswap(Fe& a, Fe& b, int swap) {
    uint64_t m = (uint64_t)(-(int64_t)swap);
    for (int i = 0; i < 4; i++) {
        uint64_t x = a.v[i], y = b.v[i];
        uint64_t t = m & (x ^ y);
        a.v[i] = x ^ t; b.v[i] = y ^ t;
    }
}

static void fe_reduce(Fe& o) {
    uint64_t h = o.v[3] >> 63;
    o.v[3] &= 0x7FFFFFFFFFFFFFFFull;
    unsigned __int128 t0 = (unsigned __int128)o.v[0] + h * 19;
    o.v[0] = (uint64_t)t0;
    unsigned __int128 t1 = (unsigned __int128)o.v[1] + (t0 >> 64);
    o.v[1] = (uint64_t)t1;
    unsigned __int128 t2 = (unsigned __int128)o.v[2] + (t1 >> 64);
    o.v[2] = (uint64_t)t2;
    unsigned __int128 t3 = (unsigned __int128)o.v[3] + (t2 >> 64);
    o.v[3] = (uint64_t)t3;
    if (t3 >> 64) {
        uint64_t f = (uint64_t)(t3 >> 64);
        unsigned __int128 u0 = (unsigned __int128)o.v[0] + f * 38;
        o.v[0] = (uint64_t)u0;
        unsigned __int128 u1 = (unsigned __int128)o.v[1] + (u0 >> 64);
        o.v[1] = (uint64_t)u1;
        unsigned __int128 u2 = (unsigned __int128)o.v[2] + (u1 >> 64);
        o.v[2] = (uint64_t)u2;
        unsigned __int128 u3 = (unsigned __int128)o.v[3] + (u2 >> 64);
        o.v[3] = (uint64_t)u3;
    }
    uint64_t b = (o.v[0] < FE_P0) ? 1 : 0;
    uint64_t d0 = o.v[0] - FE_P0;
    uint64_t d1 = o.v[1] - 0xFFFFFFFFFFFFFFFFull - b;
    uint64_t b1 = ((o.v[1] < 0xFFFFFFFFFFFFFFFFull) || (o.v[1] == 0xFFFFFFFFFFFFFFFFull && b)) ? 1 : 0;
    uint64_t d2 = o.v[2] - 0xFFFFFFFFFFFFFFFFull - b1;
    uint64_t b2 = ((o.v[2] < 0xFFFFFFFFFFFFFFFFull) || (o.v[2] == 0xFFFFFFFFFFFFFFFFull && b1)) ? 1 : 0;
    uint64_t d3 = o.v[3] - 0x7FFFFFFFFFFFFFFFull - b2;
    uint64_t b3 = ((o.v[3] < 0x7FFFFFFFFFFFFFFFull) || (o.v[3] == 0x7FFFFFFFFFFFFFFFull && b2)) ? 1 : 0;
    if (!b3) { o.v[0]=d0; o.v[1]=d1; o.v[2]=d2; o.v[3]=d3; }
}

static void fe_add(Fe& o, const Fe& a, const Fe& b) {
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) { unsigned __int128 s = (unsigned __int128)a.v[i] + b.v[i] + c; o.v[i] = (uint64_t)s; c = s >> 64; }
    if (c) {
        unsigned __int128 s = (unsigned __int128)o.v[0] + (uint64_t)c * 38;
        o.v[0] = (uint64_t)s;
        if (s >> 64) {
            unsigned __int128 t = (unsigned __int128)o.v[1] + (s >> 64);
            o.v[1] = (uint64_t)t;
            unsigned __int128 u = (unsigned __int128)o.v[2] + (t >> 64);
            o.v[2] = (uint64_t)u;
            unsigned __int128 v = (unsigned __int128)o.v[3] + (u >> 64);
            o.v[3] = (uint64_t)v;
        }
    }
    fe_reduce(o);
}

static void fe_sub(Fe& o, const Fe& a, const Fe& b) {
    uint64_t s0 = a.v[0] - b.v[0];
    uint64_t br = (a.v[0] < b.v[0]) ? 1 : 0;
    uint64_t s1 = a.v[1] - b.v[1];
    uint64_t br1 = ((a.v[1] < b.v[1]) || (a.v[1] == b.v[1] && br)) ? 1 : 0;
    s1 -= br;
    uint64_t s2 = a.v[2] - b.v[2];
    uint64_t br2 = ((a.v[2] < b.v[2]) || (a.v[2] == b.v[2] && br1)) ? 1 : 0;
    s2 -= br1;
    uint64_t s3 = a.v[3] - b.v[3];
    uint64_t br3 = ((a.v[3] < b.v[3]) || (a.v[3] == b.v[3] && br2)) ? 1 : 0;
    s3 -= br2;
    if (br3) {
        uint64_t b2 = (s0 < 38) ? 1 : 0; s0 -= 38;
        uint64_t b3 = (s1 < b2) ? 1 : 0; s1 -= b2;
        uint64_t b4 = (s2 < b3) ? 1 : 0; s2 -= b3;
        s3 -= b4;
    }
    o.v[0]=s0; o.v[1]=s1; o.v[2]=s2; o.v[3]=s3;
    fe_reduce(o);
}

static void fe_mul(Fe& o, const Fe& a, const Fe& b) {
    // Textbook schoolbook: 64-bit limbs, u128 products, uint64 carry chains.
    // No 128-bit accumulator can wrap: each r[k] stays a true 64-bit limb.
    uint64_t r[9] = {0};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            unsigned __int128 p = (unsigned __int128)a.v[i] * b.v[j];
            uint64_t lo = (uint64_t)p;
            uint64_t hi = (uint64_t)(p >> 64);
            int k = i + j;
            uint64_t c = lo;
            while (c) {
                uint64_t nv = r[k] + c;
                r[k] = nv;
                c = (nv < c) ? 1 : 0;
                k++;
            }
            k = i + j + 1;
            c = hi;
            while (c) {
                uint64_t nv = r[k] + c;
                r[k] = nv;
                c = (nv < c) ? 1 : 0;
                k++;
            }
        }
    // fold 2^256 = 38, 2^512 = 1444 (2^512 = 2^256*2^256 = 38*38)
    unsigned __int128 f0 = (unsigned __int128)r[0] + (unsigned __int128)r[4]*38 + (unsigned __int128)r[8]*1444;
    unsigned __int128 f1 = (unsigned __int128)r[1] + (unsigned __int128)r[5]*38;
    unsigned __int128 f2 = (unsigned __int128)r[2] + (unsigned __int128)r[6]*38;
    unsigned __int128 f3 = (unsigned __int128)r[3] + (unsigned __int128)r[7]*38;
    uint64_t o0 = (uint64_t)f0;
    unsigned __int128 g1 = f1 + (f0 >> 64);
    uint64_t o1 = (uint64_t)g1;
    unsigned __int128 g2 = f2 + (g1 >> 64);
    uint64_t o2 = (uint64_t)g2;
    unsigned __int128 g3 = f3 + (g2 >> 64);
    uint64_t o3 = (uint64_t)g3;
    if (g3 >> 64) {
        unsigned __int128 h = (unsigned __int128)o0 + ((g3 >> 64) * 38);
        o0 = (uint64_t)h;
        unsigned __int128 h1 = (unsigned __int128)o1 + (h >> 64);
        o1 = (uint64_t)h1;
        unsigned __int128 h2 = (unsigned __int128)o2 + (h1 >> 64);
        o2 = (uint64_t)h2;
        unsigned __int128 h3 = (unsigned __int128)o3 + (h2 >> 64);
        o3 = (uint64_t)h3;
    }
    Fe t; t.v[0]=o0; t.v[1]=o1; t.v[2]=o2; t.v[3]=o3;
    fe_reduce(t);
    fe_copy(o, t);
}

static void fe_sq(Fe& o, const Fe& a) { fe_mul(o, a, a); }

static void fe_invert(Fe& o, const Fe& a) {
    Fe t; fe_copy(t, a);
    Fe res; res.v[0]=1; res.v[1]=0; res.v[2]=0; res.v[3]=0;
    for (int i = 254; i >= 0; i--) {
        fe_sq(res, res);
        int bit = (i >= 8) ? 1 : ((0xEB >> i) & 1);
        if (bit) fe_mul(res, res, t);
    }
    fe_copy(o, res);
}

static void fe_from_bytes(Fe& o, const uint8_t b[32]) {
    o.v[0] = (uint64_t)b[0] | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
             ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) | ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
    o.v[1] = (uint64_t)b[8] | ((uint64_t)b[9] << 8) | ((uint64_t)b[10] << 16) | ((uint64_t)b[11] << 24) |
             ((uint64_t)b[12] << 32) | ((uint64_t)b[13] << 40) | ((uint64_t)b[14] << 48) | ((uint64_t)b[15] << 56);
    o.v[2] = (uint64_t)b[16] | ((uint64_t)b[17] << 8) | ((uint64_t)b[18] << 16) | ((uint64_t)b[19] << 24) |
             ((uint64_t)b[20] << 32) | ((uint64_t)b[21] << 40) | ((uint64_t)b[22] << 48) | ((uint64_t)b[23] << 56);
    o.v[3] = (uint64_t)b[24] | ((uint64_t)b[25] << 8) | ((uint64_t)b[26] << 16) | ((uint64_t)b[27] << 24) |
             ((uint64_t)b[28] << 32) | ((uint64_t)b[29] << 40) | ((uint64_t)b[30] << 48) | ((uint64_t)b[31] << 56);
    o.v[3] &= 0x7FFFFFFFFFFFFFFFull;
    fe_reduce(o);
}

static void fe_to_bytes(uint8_t b[32], const Fe& a) {
    for (int i = 0; i < 4; i++) {
        uint64_t c = a.v[i];
        b[8*i]   = (uint8_t)c;
        b[8*i+1] = (uint8_t)(c >> 8);
        b[8*i+2] = (uint8_t)(c >> 16);
        b[8*i+3] = (uint8_t)(c >> 24);
        b[8*i+4] = (uint8_t)(c >> 32);
        b[8*i+5] = (uint8_t)(c >> 40);
        b[8*i+6] = (uint8_t)(c >> 48);
        b[8*i+7] = (uint8_t)(c >> 56);
    }
}

void nefu_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u_in[32]) {
    uint8_t k[32]; memcpy(k, scalar, 32);
    k[0] &= 248; k[31] &= 127; k[31] |= 64;
    Fe x1; fe_from_bytes(x1, u_in);
    Fe x2; x2.v[0]=1; x2.v[1]=0; x2.v[2]=0; x2.v[3]=0;
    Fe z2; z2.v[0]=0; z2.v[1]=0; z2.v[2]=0; z2.v[3]=0;
    Fe x3; fe_copy(x3, x1);
    Fe z3; z3.v[0]=1; z3.v[1]=0; z3.v[2]=0; z3.v[3]=0;
    Fe a24; a24.v[0]=121665; a24.v[1]=0; a24.v[2]=0; a24.v[3]=0;
    int swap = 0;
    for (int t = 254; t >= 0; t--) {
        int kt = (k[t >> 3] >> (t & 7)) & 1;
        swap ^= kt;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = kt;
        Fe A, B, AA, BB, E, C, D, DA, CB;
        fe_add(A, x2, z2); fe_sq(AA, A);
        fe_sub(B, x2, z2); fe_sq(BB, B);
        fe_sub(E, AA, BB);
        fe_add(C, x3, z3);
        fe_sub(D, x3, z3);
        fe_mul(DA, D, A);
        fe_mul(CB, C, B);
        Fe t0, t1;
        fe_add(t0, DA, CB); fe_sq(x3, t0);
        fe_sub(t1, DA, CB); fe_sq(t1, t1); fe_mul(z3, x1, t1);
        fe_mul(x2, AA, BB);
        Fe a24e;
        fe_mul(a24e, a24, E);
        fe_add(t0, AA, a24e);
        fe_mul(z2, E, t0);
    }
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);
    Fe zi; fe_invert(zi, z2);
    Fe r; fe_mul(r, x2, zi);
    fe_to_bytes(out, r);
}

static void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    uint8_t base[32]; memset(base, 0, 32); base[0] = 9;
    nefu_x25519(out, scalar, base);
}


static void hkdf_extract(const uint8_t* salt, int saltlen,
                         const uint8_t* ikm, int ikmlen, uint8_t prk[32]) {
    uint8_t z[32]; memset(z, 0, 32);
    if (!salt || saltlen == 0) { salt = z; saltlen = 32; }
    crypto::hmac_sha256(salt, saltlen, ikm, ikmlen, prk);
}

static void hkdf_expand(const uint8_t prk[32], const uint8_t* info, int infolen,
                        uint8_t* okm, int oklen) {
    uint8_t t[32]; int tlen = 0; int off = 0;
    uint8_t counter = 1;
    while (off < oklen) {
        uint8_t msg[32 + 256 + 1];
        int ml = 0;
        if (tlen > 0) { memcpy(msg + ml, t, (size_t)tlen); ml += tlen; }
        if (infolen > 0) { memcpy(msg + ml, info, (size_t)infolen); ml += infolen; }
        msg[ml++] = counter;
        uint8_t mac[32];
        crypto::hmac_sha256(prk, 32, msg, ml, mac);
        memcpy(t, mac, 32); tlen = 32;
        int n = oklen - off; if (n > 32) n = 32;
        memcpy(okm + off, mac, (size_t)n); off += n;
        counter++;
    }
}

static void hkdf_expand_label(const uint8_t secret[32], const char* label,
                              const uint8_t* ctx, int ctxlen,
                              uint8_t* out, int outlen) {
    static const char pfx[] = "tls13 ";
    int lblen = (int)strlen(label);
    uint8_t info[2 + 1 + (sizeof(pfx)-1) + 64 + 1 + 64];
    int il = 0;
    info[il++] = (uint8_t)(outlen >> 8); info[il++] = (uint8_t)(outlen & 0xFF);
    info[il++] = (uint8_t)((sizeof(pfx)-1) + lblen);
    memcpy(info + il, pfx, sizeof(pfx)-1); il += (int)(sizeof(pfx)-1);
    memcpy(info + il, label, (size_t)lblen); il += lblen;
    info[il++] = (uint8_t)ctxlen;
    if (ctxlen > 0) { memcpy(info + il, ctx, (size_t)ctxlen); il += ctxlen; }
    hkdf_expand(secret, info, il, out, outlen);
}

// ============================================================
// AEAD
// ============================================================
enum { AEAD_AES128GCM = 1, AEAD_CHACHA20 = 2 };
static void inc32(uint8_t cb[16]);

static void gcm_mul(uint8_t out[16], const uint8_t a[16], const uint8_t b[16]) {
    uint8_t v[16]; memcpy(v, b, 16);
    uint8_t z[16]; memset(z, 0, 16);
    // GHASH field mult: bits from a[0] MSB .. a[15] LSB (big-endian 128-bit int);
    // V shifts right; bit dropped from v[15] bit0 feeds the 0xE1 reduction.
    for (int i = 0; i < 16; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if ((a[i] >> bit) & 1)
                for (int j = 0; j < 16; j++) z[j] ^= v[j];
            uint8_t carry = v[15] & 1;
            for (int j = 15; j >= 1; j--)
                v[j] = (uint8_t)((v[j] >> 1) | ((v[j-1] & 1) << 7));
            v[0] = (uint8_t)(v[0] >> 1);
            if (carry) v[0] ^= 0xE1;
        }
    }
    memcpy(out, z, 16);
}

static void gcm_ghash(uint8_t out[16], const uint8_t h[16],
                      const uint8_t* aad, int aadlen,
                      const uint8_t* c, int clen) {
    uint8_t x[16]; memset(x, 0, 16);
    int pos = 0;
    while (pos < aadlen) {
        int n = aadlen - pos; if (n > 16) n = 16;
        for (int j = 0; j < n; j++) x[j] ^= aad[pos + j];
        gcm_mul(x, x, h);
        pos += n;
    }
    pos = 0;
    while (pos < clen) {
        int n = clen - pos; if (n > 16) n = 16;
        for (int j = 0; j < n; j++) x[j] ^= c[pos + j];
        gcm_mul(x, x, h);
        pos += n;
    }
    uint8_t lenb[16]; memset(lenb, 0, 16);
    uint64_t ab = (uint64_t)aadlen * 8;
    uint64_t cb = (uint64_t)clen * 8;
    for (int i = 0; i < 8; i++) { lenb[i] = (uint8_t)(ab >> (56 - 8*i)); }
    for (int i = 0; i < 8; i++) { lenb[8+i] = (uint8_t)(cb >> (56 - 8*i)); }
    for (int j = 0; j < 16; j++) x[j] ^= lenb[j];
    gcm_mul(x, x, h);
    memcpy(out, x, 16);
}
static void inc32(uint8_t cb[16]) {
    for (int i = 15; i >= 12; i--) { cb[i]++; if (cb[i] != 0) break; }
}


static void aes128_gcm_seal(const uint8_t key[16], const uint8_t iv[12],
                            const uint8_t* aad, int aadlen,
                            const uint8_t* in, int inlen,
                            uint8_t* out, uint8_t tag[16]) {
    crypto::AES aes; aes.init(key, 16);
    uint8_t zero[16]; memset(zero, 0, 16);
    uint8_t h[16]; aes.encrypt_block(zero, h);
    uint8_t j0[16]; memcpy(j0, iv, 12); j0[12]=0; j0[13]=0; j0[14]=0; j0[15]=1;
    uint8_t cb[16]; memcpy(cb, j0, 16);
    inc32(cb);   // first keystream block = E(inc32(J0)) per GCM (J0 = IV||1)
    int pos = 0;
    while (pos < inlen) {
        uint8_t ks[16]; aes.encrypt_block(cb, ks);
        inc32(cb);
        int n = inlen - pos; if (n > 16) n = 16;
        for (int j = 0; j < n; j++) out[pos + j] = (uint8_t)(in[pos + j] ^ ks[j]);
        pos += n;
    }
    uint8_t s[16]; aes.encrypt_block(j0, s);
    uint8_t g[16]; gcm_ghash(g, h, aad, aadlen, out, inlen);
    for (int j = 0; j < 16; j++) tag[j] = (uint8_t)(g[j] ^ s[j]);
}

static int aes128_gcm_open(const uint8_t key[16], const uint8_t iv[12],
                           const uint8_t* aad, int aadlen,
                           const uint8_t* in, int inlen,
                           const uint8_t tag[16], uint8_t* out) {
    crypto::AES aes; aes.init(key, 16);
    uint8_t zero[16]; memset(zero, 0, 16);
    uint8_t h[16]; aes.encrypt_block(zero, h);
    uint8_t j0[16]; memcpy(j0, iv, 12); j0[12]=0; j0[13]=0; j0[14]=0; j0[15]=1;
    uint8_t cb[16]; memcpy(cb, j0, 16);
    inc32(cb);   // first keystream block = E(inc32(J0)) per GCM (J0 = IV||1)
    int pos = 0;
    while (pos < inlen) {
        uint8_t ks[16]; aes.encrypt_block(cb, ks);
        inc32(cb);
        int n = inlen - pos; if (n > 16) n = 16;
        for (int j = 0; j < n; j++) out[pos + j] = (uint8_t)(in[pos + j] ^ ks[j]);
        pos += n;
    }
    uint8_t s[16]; aes.encrypt_block(j0, s);
    uint8_t g[16]; gcm_ghash(g, h, aad, aadlen, in, inlen);
    uint8_t diff = 0;
    for (int j = 0; j < 16; j++) diff |= (uint8_t)((g[j] ^ s[j]) ^ tag[j]);
    return diff == 0 ? 0 : -1;
}
    
static void chacha20_poly1305_seal(const uint8_t key[32], const uint8_t iv[12],
                                   const uint8_t* aad, int aadlen,
                                   const uint8_t* in, int inlen,
                                   uint8_t* out, uint8_t tag[16]) {
    uint8_t polykey[64];
    crypto::chacha20_block(key, 0, iv, polykey);
    crypto::chacha20_crypt(key, iv, 1, in, out, inlen);
    int aadpad = (16 - (aadlen & 15)) & 15;
    int cpad = (16 - (inlen & 15)) & 15;
    int total = aadlen + aadpad + inlen + cpad + 16;
    uint8_t buf[16384 + 512];
    memset(buf, 0, sizeof(buf));   // zero the pad regions: poly1305 MAC input
    int p = 0;
    if (aadlen) { memcpy(buf + p, aad, (size_t)aadlen); p += aadlen; }
    p += aadpad;
    if (inlen) { memcpy(buf + p, out, (size_t)inlen); p += inlen; }
    p += cpad;
    uint64_t la = (uint64_t)aadlen, lc = (uint64_t)inlen;
    for (int i = 0; i < 8; i++) buf[p++] = (uint8_t)(la >> (8*i));
    for (int i = 0; i < 8; i++) buf[p++] = (uint8_t)(lc >> (8*i));
    (void)total;
    crypto::poly1305_mac(polykey, buf, p, tag);
}

static int chacha20_poly1305_open(const uint8_t key[32], const uint8_t iv[12],
                                  const uint8_t* aad, int aadlen,
                                  const uint8_t* in, int inlen,
                                  const uint8_t tag[16], uint8_t* out) {
    // compute the expected tag over the CIPHERTEXT (RFC 8439 AEAD): the
    // tag authenticates (aad || ct). sealing reuses out for the ct, but in
    // the open direction out holds the decrypted plaintext, so we must
    // build the mac input from `in` directly instead of reusing seal.
    uint8_t polykey[64];
    crypto::chacha20_block(key, 0, iv, polykey);
    int aadpad = (16 - (aadlen & 15)) & 15;
    int cpad = (16 - (inlen & 15)) & 15;
    uint8_t buf[16384 + 512];
    memset(buf, 0, sizeof(buf));   // zero the pad regions: poly1305 MAC input
    int p = 0;
    if (aadlen) { memcpy(buf + p, aad, (size_t)aadlen); p += aadlen; }
    p += aadpad;
    if (inlen) { memcpy(buf + p, in, (size_t)inlen); p += inlen; }
    p += cpad;
    uint64_t la = (uint64_t)aadlen, lc = (uint64_t)inlen;
    for (int i = 0; i < 8; i++) buf[p++] = (uint8_t)(la >> (8*i));
    for (int i = 0; i < 8; i++) buf[p++] = (uint8_t)(lc >> (8*i));
    uint8_t t2[16];
    crypto::poly1305_mac(polykey, buf, p, t2);
    uint8_t diff = 0;
    for (int j = 0; j < 16; j++) diff |= (uint8_t)(t2[j] ^ tag[j]);
    if (diff) return -1;
    crypto::chacha20_crypt(key, iv, 1, in, out, inlen);
    return 0;
}

// ============================================================
// TLS 1.3 record / handshake layer
// ============================================================
#define TLS_RT_HANDSHAKE 22
#define TLS_RT_APP       23
#define TLS_RT_CCS       20
#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_ENCRYPTED_EXT 8
#define TLS_HS_CERTIFICATE   11
#define TLS_HS_CERT_VERIFY   15
#define TLS_HS_FINISHED      20

struct Tls13Conn {
    tls_recv_fn rf;
    tls_send_fn sf;
    void* ctx;
    int alg;
    // handshake traffic secrets + keys
    uint8_t c_hs_sec[32], s_hs_sec[32];
    uint8_t c_hs_key[32], c_hs_iv[12];
    uint8_t s_hs_key[32], s_hs_iv[12];
    // application traffic secrets + keys
    uint8_t c_ap_sec[32], s_ap_sec[32];
    uint8_t c_ap_key[32], c_ap_iv[12];
    uint8_t s_ap_key[32], s_ap_iv[12];
    uint64_t hs_send_seq, hs_recv_seq, ap_send_seq, ap_recv_seq;
    // transcript
    nefu_sha256_ctx th;
    // raw record buffer + decrypted plaintext stream buffer
    uint8_t rec_hdr[5];       // current record header (AAD for TLS1.3 AEAD)
    uint8_t rbuf[16384 + 256];
    uint8_t dec[65536 + 256];
    int dec_len, dec_pos;
    int hs_encrypted;    // 0: ServerHello still plaintext, 1: encrypted handshake
    int keylen;          // 16 (AES-128-GCM) or 32 (ChaCha20)
    char alpn[16];       // server-selected ALPN protocol ("" if none)
    char err[128];
};

static void th_update(Tls13Conn* c, const uint8_t* p, int n) { nefu_sha256_update(&c->th, p, (uint32_t)n); }

static void th_append(Tls13Conn* c, const uint8_t* m, int len) {
    nefu_sha256_update(&c->th, m, (uint32_t)len);
}

static void th_hash(Tls13Conn* c, uint8_t out[32]) {
    nefu_sha256_ctx t = c->th;
    nefu_sha256_final(&t, out);
}

// rver: legacy_record_version (RFC 8446 5.1: ClientHello MUST use 0x0301;
// all other records use 0x0303).
static int tls_send_record_v(Tls13Conn* c, int type, int rver,
                             const uint8_t* payload, int len) {
    uint8_t hdr[5];
    hdr[0] = (uint8_t)type; hdr[1] = (uint8_t)(rver >> 8); hdr[2] = (uint8_t)(rver & 0xFF);
    hdr[3] = (uint8_t)(len >> 8); hdr[4] = (uint8_t)(len & 0xFF);
    uint8_t tmp[5 + 16384 + 256];
    memcpy(tmp, hdr, 5);
    memcpy(tmp + 5, payload, (size_t)len);
    return c->sf(c->ctx, tmp, 5 + len);
}

static int tls_send_record(Tls13Conn* c, int type, const uint8_t* payload, int len) {
    return tls_send_record_v(c, type, 0x0303, payload, len);
}

// encrypted record with the given traffic key/iv
static int tls_send_encrypted_rec(Tls13Conn* c, const uint8_t key[32], const uint8_t iv[12],
                                  uint64_t* seqp, const uint8_t* plain, int len,
                                  int content_type) {
    uint8_t body[16384 + 1 + 16];
    memcpy(body, plain, (size_t)len);
    body[len] = (uint8_t)content_type;
    uint8_t nonce[12];
    for (int i = 0; i < 12; i++) nonce[i] = iv[i];
    uint64_t seq = (*seqp)++;
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(seq >> (8*i));
    uint8_t ct[16384 + 1 + 16], tag[16];
    int ptlen = len + 1;
    uint8_t aad[5];                        // TLS1.3 AAD = record header (RFC 8446 5.2)
    aad[0] = TLS_RT_APP; aad[1] = 3; aad[2] = 3;
    aad[3] = (uint8_t)(((ptlen + 16) >> 8) & 0xFF); aad[4] = (uint8_t)((ptlen + 16) & 0xFF);
    if (c->alg == AEAD_AES128GCM)
        aes128_gcm_seal(key, nonce, aad, 5, body, ptlen, ct, tag);
    else
        chacha20_poly1305_seal(key, nonce, aad, 5, body, ptlen, ct, tag);
    uint8_t out[16384 + 1 + 16 + 16];
    memcpy(out, ct, (size_t)ptlen);
    memcpy(out + ptlen, tag, 16);
    return tls_send_record(c, TLS_RT_APP, out, ptlen + 16);
}

static int tls_read_exact(Tls13Conn* c, uint8_t* buf, int n, int timeout_ms) {
    int got = 0;
    while (got < n) {
        int r = c->rf(c->ctx, buf + got, n - got, timeout_ms);
        if (r <= 0) {
            snprintf(c->err, sizeof(c->err), "recv short: got %d want %d (rf=%d)", got, n, r);
            return -1;
        }
        got += r;
    }
    return got;
}

static int tls_read_record(Tls13Conn* c, int* type, uint8_t** payload, int* plen, int timeout_ms) {
    uint8_t hdr[5];
    if (tls_read_exact(c, hdr, 5, timeout_ms) < 0) return -1;
#ifdef NEFU_DUMP_REC
    fprintf(stderr, "[rec] hdr=%02x %02x%02x %02x%02x\n", hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);
#endif
    memcpy(c->rec_hdr, hdr, 5);
    *type = hdr[0];
    int len = (hdr[3] << 8) | hdr[4];
    if (len < 0 || len > (int)sizeof(c->rbuf)) {
        snprintf(c->err, sizeof(c->err), "bad record len %d type %d", len, hdr[0]);
        return -1;
    }
    if (tls_read_exact(c, c->rbuf, len, timeout_ms) < 0) return -1;
#ifdef NEFU_DUMP_REC
    {
        int dn = len < 16 ? len : 16;
        fprintf(stderr, "[rec] payload[%d]: ", len);
        for (int i = 0; i < dn; i++) fprintf(stderr, "%02x ", c->rbuf[i]);
        fprintf(stderr, "\n");
    }
#endif
    *payload = c->rbuf;
    *plen = len;
    return 0;
}

static int tls_decrypt_into(Tls13Conn* c, const uint8_t key[32], const uint8_t iv[12],
                            uint64_t seq, const uint8_t* payload, int plen,
                            uint8_t* out, int* outlen) {
    if (plen < 17) return -1;
    int ctlen = plen - 16;
    uint8_t nonce[12];
    for (int i = 0; i < 12; i++) nonce[i] = iv[i];
    for (int i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(seq >> (8*i));
    const uint8_t* tag = payload + ctlen;
    int rc;
    if (c->alg == AEAD_AES128GCM)
        rc = aes128_gcm_open(key, nonce, c->rec_hdr, 5, payload, ctlen, tag, out);
    else
        rc = chacha20_poly1305_open(key, nonce, c->rec_hdr, 5, payload, ctlen, tag, out);
    if (rc != 0) return -1;
    *outlen = ctlen - 1;   // strip content-type byte
    return out[ctlen - 1]; // inner content type
}

// ---- handshake message reader (plaintext or encrypted phase) ----
struct HsBuf {
    uint8_t data[65536 + 256];
    int len;      // bytes consumed so far (4 hdr + body)
    int want;     // total body bytes (-1: header not complete)
    int msgtype;
};

// Pull bytes from the decrypted plaintext stream, reading+decrypting records as needed.
static int tls_fill_plain(Tls13Conn* c, uint8_t* dst, int n, int timeout_ms) {
    while (c->dec_len - c->dec_pos < n) {
        // shift remainder
        int rem = c->dec_len - c->dec_pos;
        if (rem > 0 && c->dec_pos > 0) {
            memmove(c->dec, c->dec + c->dec_pos, (size_t)rem);
            c->dec_len = rem; c->dec_pos = 0;
        }
        int type; uint8_t* pl; int plen;
        if (tls_read_record(c, &type, &pl, &plen, timeout_ms) < 0) return -1;
        if (c->hs_encrypted) {
            if (type == TLS_RT_CCS) continue;   // TLS 1.3 compat: skip ChangeCipherSpec
            if (type != TLS_RT_APP) {
                snprintf(c->err, sizeof(c->err), "hs enc rec type=%d (want app)", type);
                return -1;
            }
            uint8_t plain[16384 + 256];
            int olen;
            int ct = tls_decrypt_into(c, c->s_hs_key, c->s_hs_iv, c->hs_recv_seq++, pl, plen, plain, &olen);
#ifdef NEFU_DUMP_REC
            fprintf(stderr, "[dec] type=%d plen=%d olen=%d seq=%llu\n", ct, plen, olen, (unsigned long long)(c->hs_recv_seq - 1));
            if (ct < 0) { fprintf(stderr, "[dec] FAILED, key=%02x%02x.. iv=%02x%02x..\n", c->s_hs_key[0], c->s_hs_key[1], c->s_hs_iv[0], c->s_hs_iv[1]);
                int dn = plen < 96 ? plen : 96;
                fprintf(stderr, "[dec] hdr=%02x %02x%02x %02x%02x ct=", c->rec_hdr[0], c->rec_hdr[1], c->rec_hdr[2], c->rec_hdr[3], c->rec_hdr[4]);
                for (int i = 0; i < dn; i++) fprintf(stderr, "%02x ", pl[i]);
                fprintf(stderr, "\n"); }
#endif
            if (ct < 0) {
                snprintf(c->err, sizeof(c->err), "hs decrypt fail type=%d plen=%d seq=%llu",
                         type, plen, (unsigned long long)(c->hs_recv_seq - 1));
                klogf("[tls] decrypt fail alg=%d keylen=%d\n", c->alg, c->keylen);
                klogf("[tls] s_hs_key=\n");
                for (int i = 0; i < c->keylen; i++) { klogf("%02x", c->s_hs_key[i]); if (i % 16 == 15) klogf("\n"); }
                klogf("[tls] s_hs_iv=");
                for (int i = 0; i < 12; i++) klogf("%02x", c->s_hs_iv[i]);
                klogf("\n[tls] rec_hdr=");
                for (int i = 0; i < 5; i++) klogf("%02x", c->rec_hdr[i]);
                {
                    // copy the ciphertext so a later recv cannot clobber rbuf
                    // while we print, and emit 16 bytes per line so the COM1
                    // log cannot truncate a 64-hex line
                    uint8_t ctcopy[512];
                    int ctn = plen < (int)sizeof(ctcopy) ? plen : (int)sizeof(ctcopy);
                    memcpy(ctcopy, pl, (size_t)ctn);
                    klogf("\n[tls] ct[%d]=\n", plen);
                    for (int i = 0; i < ctn; i++) { klogf("%02x", ctcopy[i]); if (i % 16 == 15) klogf("\n"); }
                    klogf("\n");
                }
                return -1;
            }
            if (ct != TLS_RT_HANDSHAKE && ct != TLS_RT_APP) return -1;
            if (c->dec_len + olen > (int)sizeof(c->dec)) return -1;
            memcpy(c->dec + c->dec_len, plain, (size_t)olen);
            c->dec_len += olen;
        } else {
            if (type == TLS_RT_CCS) continue;   // plaintext phase: server may send compat CCS before/around ServerHello
            if (type != TLS_RT_HANDSHAKE) return -1;
            if (c->dec_len + plen > (int)sizeof(c->dec)) return -1;
            memcpy(c->dec + c->dec_len, pl, (size_t)plen);
            c->dec_len += plen;
        }
    }
    memcpy(dst, c->dec + c->dec_pos, (size_t)n);
    c->dec_pos += n;
    return 0;
}

// read one complete handshake message (header + body)
static int tls_read_hs_msg(Tls13Conn* c, HsBuf& hb, int timeout_ms) {
    hb.len = 0; hb.want = -1;
    for (;;) {
        if (hb.want < 0) {
            uint8_t h4[4];
            if (tls_fill_plain(c, h4, 4, timeout_ms) < 0) {
                // keep the inner error verbatim (no recursive re-wrap that
                // eats the whole 160-byte diagnostic buffer)
                if (c->err[0] == 0) snprintf(c->err, sizeof(c->err), "hs header read fail");
                return -1;
            }
            hb.data[0]=h4[0]; hb.data[1]=h4[1]; hb.data[2]=h4[2]; hb.data[3]=h4[3];
            hb.len = 4;
            int mlen = (h4[1] << 16) | (h4[2] << 8) | h4[3];
            if (mlen > 65535) return -1;
            hb.want = mlen;
            if (mlen == 0) return 0;
        }
        if (hb.len - 4 < hb.want) {
            int need = hb.want - (hb.len - 4);
            int chunk = need > 4096 ? 4096 : need;
            if (tls_fill_plain(c, hb.data + hb.len, chunk, timeout_ms) < 0) return -1;
            hb.len += chunk;
        } else break;
    }
    return 0;
}

static void put16(uint8_t*& p, int v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)(v&0xFF); p+=2; }
static void put24(uint8_t*& p, int v) { p[0]=(uint8_t)(v>>16); p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v&0xFF); p+=3; }
static void put16v(uint8_t* p, int v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)(v&0xFF); }
static void put24v(uint8_t* p, int v) { p[0]=(uint8_t)(v>>16); p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v&0xFF); }
static int get16(const uint8_t* p) { return (p[0]<<8)|p[1]; }
static int get24(const uint8_t* p) { return (p[0]<<16)|(p[1]<<8)|p[2]; }

static void rand_bytes(uint8_t* out, int n) {
    static uint64_t st[4] = {0x9E3779B97F4A7C15ull, 0xBF58476D1CE4E5B9ull, 0x94D049BB133111EBull, 0xBF58476D1CE4E5B9ull};
    st[0] ^= (uint64_t)out; st[1] ^= (uint64_t)&st; st[2] += 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < n; i += 8) {
        uint64_t x = crypto::xoshiro256starstar(st);
        for (int j = 0; j < 8 && i + j < n; j++) out[i+j] = (uint8_t)(x >> (8*j));
    }
}

// ============================================================
// ClientHello (Chrome 122-ish fingerprint)
// ============================================================
static int build_client_hello(uint8_t* out, int outsz, const char* hostname,
                              const uint8_t* pubkey, int pubkey_len,
                              uint8_t random_out[32]) {
    uint8_t body[1024];
    uint8_t* p = body;
    put16(p, 0x0303); // legacy_version
    uint8_t rnd[32];
    rand_bytes(rnd, 32);
    memcpy(random_out, rnd, 32);
    memcpy(p, rnd, 32); p += 32;
    uint8_t sid[32];
    rand_bytes(sid, 32);
    p[0] = 32; p += 1;
    memcpy(p, sid, 32); p += 32;
    // cipher suites (GREASE + Chrome order, minus TLS_AES_256_GCM_SHA384:
    // the bare-metal crypto stack only implements AES-128-GCM / ChaCha20,
    // so do not advertise a suite we cannot actually use)
    static const uint16_t suites[] = {
        0x0a0a, 0x1301, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC02E,
        0xCCA9, 0xCCA8, 0xC014, 0xC013, 0x009C, 0x009D, 0x002F, 0x0035, 0x000A
    };
    int nsuites = (int)(sizeof(suites)/sizeof(suites[0]));
    put16(p, nsuites*2);
    for (int i = 0; i < nsuites; i++) put16(p, suites[i]);
    p[0] = 1; p[1] = 0; p += 2; // compression methods: null

    uint8_t ext[1024];
    uint8_t* e = ext;
    // GREASE
    put16(e, 0x0a0a); put16(e, 0);
    // server_name (RFC 6066, ext id 0x0000)
    {
        int hn = (int)strlen(hostname);
        uint8_t* t = e + 4;
        t[0] = (uint8_t)((hn + 3) >> 8); t[1] = (uint8_t)(hn + 3); t[2] = 0; t += 3;  // list len (hn+3), name_type host_name
        t += 0;
        t[0] = (uint8_t)(hn >> 8); t[1] = (uint8_t)hn; t += 2;                          // name len
        memcpy(t, hostname, (size_t)hn); t += hn;                                       // name
        int llen = (int)(t - (e + 4));
        e[0] = 0; e[1] = 0;                                                             // ext id server_name
        e[2] = (uint8_t)(llen >> 8); e[3] = (uint8_t)llen;                              // ext len
        e = t;
    }
    // psk_key_exchange_modes (RFC 8446 4.2.9, ext id 0x002d)
    {
        uint8_t* q = e;
        put16(q, 0x002d);        // q now points at the length field
        uint8_t* t = q;
        uint8_t* u = t + 2;      // data
        u[0] = 1; u[1] = 0x01; u += 2;   // 1 mode: psk_dhe_ke
        put16(q, (int)(u - (t + 2)));    // length = 2
        e = u;
    }
    // supported_versions
    {
        put16(e, 0x002b);
        uint8_t* t = e + 2;
        t[0] = 2; t[1] = 0x03; t[2] = 0x04; t += 3;
        put16(e, (int)(t - (e + 2)));
        e = t;
    }
    // key_share
    {
        uint8_t* es = e;
        put16(e, 0x0033);
        uint8_t* t = e + 2;
        put16(t, 2 + 2 + pubkey_len);
        put16(t, 0x001D);
        put16(t, pubkey_len);
        memcpy(t, pubkey, (size_t)pubkey_len); t += pubkey_len;
        { uint8_t* q = es + 2; put16(q, (int)(t - (es + 4))); }
        e = t;
    }
    // supported_groups (RFC 8446 4.2.7: NamedGroup groups<2..2^16-1>, 2-byte len)
    {
        uint8_t* q = e;
        put16(q, 0x000a);
        uint8_t* t = q;
        uint8_t* u = t + 2;
        u[0] = 0; u[1] = 2; u[2] = 0x00; u[3] = 0x1D; u += 4;   // len=2, x25519
        put16(q, (int)(u - (t + 2)));
        e = u;
    }
    // signature_algorithms
    {
        put16(e, 0x000d);
        static const uint16_t sas[] = {0x0403,0x0804,0x0401,0x0503,0x0805,0x0501,0x0806,0x0601};
        uint8_t* t = e + 2;
        put16(t, (int)(sizeof(sas)/sizeof(sas[0])) * 2);
        for (int i = 0; i < (int)(sizeof(sas)/sizeof(sas[0])); i++) put16(t, sas[i]);
        put16(e, (int)(t - (e + 2)));
        e = t;
    }
    // ec_point_formats (RFC 4492 5.1.2: PointFormat list<1..2^16-1>, 2-byte len)
    {
        uint8_t* q = e;
        put16(q, 0x000b);
        uint8_t* t = q;
        uint8_t* u = t + 2;
        u[0] = 0; u[1] = 1; u[2] = 0; u += 3;   // len=1, uncompressed(0)
        put16(q, (int)(u - (t + 2)));
        e = u;
    }
    // extended_master_secret (RFC 7627, ext id 0x0017, empty body)
    {
        uint8_t* q = e;
        put16(q, 0x0017);
        put16(q, 0);
        e = q;
    }
    // session_ticket (RFC 5077, ext id 0x0023, empty body)
    {
        uint8_t* q = e;
        put16(q, 0x0023);
        put16(q, 0);
        e = q;
    }
    // ALPN (RFC 7301 3.3.1): ProtocolNameList len = 2 bytes, each ProtocolName len = 1 byte
    {
        uint8_t* q = e;
        put16(q, 0x0010);
        uint8_t* t = q;
        uint8_t* u = t + 2;
        u[0] = 0; u[1] = 12;                    // ProtocolNameList length = 12
        u[2] = 2; memcpy(u + 3, "h2", 2);       // ProtocolName: len 2 + "h2"
        u[5] = 8; memcpy(u + 6, "http/1.1", 8); // ProtocolName: len 8 + "http/1.1"
        u += 14;                                // 2 + 1 + 2 + 1 + 8
        put16(q, (int)(u - (t + 2)));           // ext len = 14
        e = u;
    }
    int extlen = (int)(e - ext);
    put16(p, extlen);
    memcpy(p, ext, (size_t)extlen); p += extlen;
    int bodylen = (int)(p - body);
    if (4 + bodylen > outsz) return -1;
    uint8_t* o = out;
    o[0] = TLS_HS_CLIENT_HELLO;
    { uint8_t* q = o + 1; put24(q, bodylen); }
    memcpy(o + 4, body, (size_t)bodylen);
    return 4 + bodylen;
}

// ============================================================
// Handshake
// ============================================================
static void derive_traffic_keys(Tls13Conn* c, const uint8_t secret[32],
                                uint8_t key[32], uint8_t iv[12]) {
    hkdf_expand_label(secret, "key", 0, 0, key, c->keylen);
    hkdf_expand_label(secret, "iv", 0, 0, iv, 12);
}

static void derive_secret(const uint8_t secret[32], const char* label,
                          Tls13Conn* c, uint8_t out[32]) {
    uint8_t th[32];
    th_hash(c, th);
    hkdf_expand_label(secret, label, th, 32, out, 32);
}

// parse ServerHello: returns 0 ok, -1 error, -2 hello retry request
static int parse_server_hello(const uint8_t* b, int blen, Tls13Conn* c,
                              uint8_t* server_pub, int* cipher) {
    const uint8_t* p = b;
    int legacy_ver = get16(p); p += 2;
    (void)legacy_ver;
    // random
    static const uint8_t hrr_random[32] = {
        0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
        0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C
    };
    if (memcmp(p, hrr_random, 32) == 0) return -2;
    p += 32;
    if (p + 1 > b + blen) { snprintf(c->err, sizeof(c->err), "sh: sidlen oob"); return -1; }
    int sidlen = *p++; 
    if (p + sidlen > b + blen) { snprintf(c->err, sizeof(c->err), "sh: sid oob"); return -1; }
    p += sidlen;
    if (p + 3 > b + blen) { snprintf(c->err, sizeof(c->err), "sh: suite oob"); return -1; }
    *cipher = get16(p); p += 2;
    int comp = *p++;
    (void)comp;
    if (p + 2 > b + blen) { snprintf(c->err, sizeof(c->err), "sh: extlen oob"); return -1; }
    // extensions
    int extlen = get16(p); p += 2;
    const uint8_t* end = p + extlen;
    if (end > b + blen) { snprintf(c->err, sizeof(c->err), "sh: extend oob"); return -1; }
    int got_keyshare = 0;
    while (p + 4 <= end) {
        int et = get16(p); p += 2;
        int el = get16(p); p += 2;
        if (p + el > end) { snprintf(c->err, sizeof(c->err), "sh: ext %04x oob", et); return -1; }
        if (et == 0x0033) { // key_share
            if (el >= 4) {
                int grp = get16(p);
                int kl = get16(p + 2);
                if (grp == 0x001D && kl == 32 && 4 + kl <= el) {
                    memcpy(server_pub, p + 4, 32);
                    got_keyshare = 1;
                }
            }
        }
        p += el;
    }
    if (!got_keyshare) { snprintf(c->err, sizeof(c->err), "sh: no keyshare"); return -1; }
    return 0;
}

// Parse the ALPN extension (0x0010) out of the first encrypted handshake
// message (EncryptedExtensions, TLS 1.3 handshake type 8).
static void parse_ee_alpn(const uint8_t* b, int blen, Tls13Conn* c) {
    if (blen < 6 || b[0] != TLS_HS_ENCRYPTED_EXT) return;
    const uint8_t* p = b + 4;              // skip type(1)+len(3)
    int extlen = get16(p); p += 2;
    const uint8_t* end = p + extlen;
    if (end > b + blen) return;
    while (p + 4 <= end) {
        int et = get16(p); p += 2;
        int el = get16(p); p += 2;
        if (p + el > end) return;
        if (et == 0x0010) {                // application_layer_protocol_negotiation
            int llen = get16(p);
            const uint8_t* q = p + 2;
            if (llen >= 1 && q + llen <= p + el) {
                int pn = q[0];             // protocol name length
                if (pn >= 1 && pn <= 15 && q + 1 + pn <= p + el) {
                    memcpy(c->alpn, q + 1, (size_t)pn);
                    c->alpn[pn] = 0;
                }
            }
            return;
        }
        p += el;
    }
}

Tls13Conn* tls13_connect(tls_recv_fn rf, tls_send_fn sf, void* ctx,
                         const char* hostname, char* err, int errsz) {
    Tls13Conn* c = new Tls13Conn();
    memset(c, 0, sizeof(*c));
    c->rf = rf; c->sf = sf; c->ctx = ctx;
    c->hs_encrypted = 0;
    c->keylen = 16;
    uint8_t server_pub[32];
    int cipher = 0;
    int rc = 0;
    if (err && errsz > 0) err[0] = 0;
    nefu_sha256_init(&c->th);

    // ---- key pair + ClientHello ----
    uint8_t priv[32]; rand_bytes(priv, 32);
    priv[0] &= 248; priv[31] &= 127; priv[31] |= 64;
    uint8_t pub[32];
    x25519_base(pub, priv);
    uint8_t rnd[32];
    uint8_t ch[1024];
    int chlen = build_client_hello(ch, (int)sizeof(ch), hostname, pub, 32, rnd);
    if (chlen < 0) { snprintf(c->err, sizeof(c->err), "client hello build"); goto fail; }
#ifdef NEFU_DUMP_CH
    { char fn[64]; snprintf(fn, sizeof(fn), "nefu_ch_%s.bin", hostname);
      FILE* fp = fopen(fn, "wb"); if (fp) { fwrite(ch, 1, (size_t)chlen, fp); fclose(fp); } }
#endif
    if (tls_send_record_v(c, TLS_RT_HANDSHAKE, 0x0301, ch, chlen) < 0) { snprintf(c->err, sizeof(c->err), "send client hello"); goto fail; }
    th_append(c, ch, chlen);

    // ---- ServerHello (plaintext) ----
    HsBuf hb;
    memset(&hb, 0, sizeof(hb));
    // keep the inner error (recv short / bad record) instead of overwriting it
    if (tls_read_hs_msg(c, hb, 10000) < 0) {
        if (c->err[0] == 0) snprintf(c->err, sizeof(c->err), "recv server hello");
        goto fail;
    }
    if (hb.data[0] != TLS_HS_SERVER_HELLO) { snprintf(c->err, sizeof(c->err), "expected server hello, got %d", hb.data[0]); goto fail; }
#ifdef NEFU_DUMP_REC
    { fprintf(stderr, "[sh] len=%d: ", hb.want);
      int dn = hb.want < 96 ? hb.want : 96;
      for (int i = 0; i < dn; i++) fprintf(stderr, "%02x ", hb.data[i]);
      fprintf(stderr, "\n");
      FILE* fp = fopen("serverhello.bin", "wb");
      if (fp) { fwrite(hb.data, 1, (size_t)(4 + hb.want), fp); fclose(fp); } }
#endif
    rc = parse_server_hello(hb.data + 4, hb.want, c, server_pub, &cipher);
    if (rc == -2) { snprintf(c->err, sizeof(c->err), "hello retry request (unsupported)"); goto fail; }
    if (rc != 0) { if (c->err[0] == 0) snprintf(c->err, sizeof(c->err), "server hello parse"); goto fail; }
    if (cipher == TLS_AES_128_GCM_SHA256) c->alg = AEAD_AES128GCM;
    else if (cipher == TLS_CHACHA20_POLY1305_SHA256) c->alg = AEAD_CHACHA20;
    else { snprintf(c->err, sizeof(c->err), "unsupported cipher 0x%04x", cipher); goto fail; }
    c->keylen = (c->alg == AEAD_AES128GCM) ? 16 : 32;
    th_append(c, hb.data, hb.len);

    // ---- ECDHE + handshake secrets ----
    uint8_t shared[32];
    nefu_x25519(shared, priv, server_pub);
    uint8_t early[32], derived[32], hs_secret[32];
    uint8_t z32[32]; memset(z32, 0, 32);
    hkdf_extract(0, 0, z32, 32, early);                  // PSK=0, IKM=Hash.length zeros
    static const uint8_t sha256_empty[32] = {   // SHA256("")
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
        0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
        0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    uint8_t d0[32]; memcpy(d0, sha256_empty, 32);
    hkdf_expand_label(early, "derived", d0, 32, derived, 32);
    hkdf_extract(derived, 32, shared, 32, hs_secret);
    derive_secret(hs_secret, "c hs traffic", c, c->c_hs_sec);
    derive_secret(hs_secret, "s hs traffic", c, c->s_hs_sec);
    derive_traffic_keys(c, c->c_hs_sec, c->c_hs_key, c->c_hs_iv);
    derive_traffic_keys(c, c->s_hs_sec, c->s_hs_key, c->s_hs_iv);
    // full-key diagnostic: on the host build stdout is lossless, so this is the
    // reference for cross-checking the bare COM1 dumps with tls_keychain.py
    klogf("[tls] alg=%d keylen=%d\n", c->alg, c->keylen);
    klogf("[tls] host s_hs_key=");
    for (int i = 0; i < c->keylen; i++) klogf("%02x", c->s_hs_key[i]);
    klogf("\n[tls] host s_hs_iv=");
    for (int i = 0; i < 12; i++) klogf("%02x", c->s_hs_iv[i]);
    klogf("\n");
    // diagnostic dumps (16 bytes per line so the COM1 log cannot truncate a
    // 64-hex line and corrupt the comparison)
    klogf("[tls] priv=\n");
    for (int i = 0; i < 32; i++) { klogf("%02x", priv[i]); if (i % 16 == 15) klogf("\n"); }
    klogf("[tls] server_pub=\n");
    for (int i = 0; i < 32; i++) { klogf("%02x", server_pub[i]); if (i % 16 == 15) klogf("\n"); }
    klogf("[tls] shared=\n");
    for (int i = 0; i < 32; i++) { klogf("%02x", shared[i]); if (i % 16 == 15) klogf("\n"); }
    {
        uint8_t thd[32]; th_hash(c, thd);
        klogf("[tls] th=\n");
        for (int i = 0; i < 32; i++) { klogf("%02x", thd[i]); if (i % 16 == 15) klogf("\n"); }
    }
    // HKDF intermediate values (16 per line; each value is printed twice so a
    // dropped nibble in one copy can be recovered from the other)
    klogf("[tls] cli_pub=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", pub[i]); if (i % 16 == 15) klogf("\n"); }
    }
    klogf("[tls] early=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", early[i]); if (i % 16 == 15) klogf("\n"); }
    }
    klogf("[tls] derived=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", derived[i]); if (i % 16 == 15) klogf("\n"); }
    }
    klogf("[tls] hs_secret=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", hs_secret[i]); if (i % 16 == 15) klogf("\n"); }
    }
    klogf("[tls] c_hs_sec=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", c->c_hs_sec[i]); if (i % 16 == 15) klogf("\n"); }
    }
    klogf("[tls] s_hs_sec=\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 32; i++) { klogf("%02x", c->s_hs_sec[i]); if (i % 16 == 15) klogf("\n"); }
    }
#ifdef NEFU_DUMP_KEY
    { FILE* fp = fopen("keys_dump.bin", "wb");
      if (fp) {
        fwrite(priv, 1, 32, fp); fwrite(server_pub, 1, 32, fp); fwrite(shared, 1, 32, fp);
        fwrite(early, 1, 32, fp); fwrite(derived, 1, 32, fp); fwrite(hs_secret, 1, 32, fp);
        fwrite(c->c_hs_sec, 1, 32, fp); fwrite(c->s_hs_sec, 1, 32, fp);
        fwrite(c->c_hs_key, 1, (size_t)c->keylen, fp); fwrite(c->c_hs_iv, 1, 12, fp);
        fwrite(c->s_hs_key, 1, (size_t)c->keylen, fp); fwrite(c->s_hs_iv, 1, 12, fp);
        uint8_t th[32]; th_hash(c, th); fwrite(th, 1, 32, fp);
        fclose(fp);
      } }
#endif

    // ---- encrypted handshake phase ----
    c->hs_encrypted = 1;
    // EncryptedExtensions (skip), Certificate (skip), CertificateVerify (skip), Finished (verify)
    // Long timeout: the server's final TLS data segment (CertificateVerify+
    // Finished records) is delayed by its RTO backoff when the client ACK is
    // dropped by slirp, reaching us ~15s after the burst. 15s sat 8ms short in
    // the 2026-10-18 run; 25s covers the backoff with margin.
    for (int i = 0; i < 3; i++) {
        if (tls_read_hs_msg(c, hb, 25000) < 0) {
            if (c->err[0] == 0) snprintf(c->err, sizeof(c->err), "recv hs msg %d", i);
            goto fail;
        }
        if (i == 0) parse_ee_alpn(hb.data, hb.len, c);   // EncryptedExtensions: ALPN
        th_append(c, hb.data, hb.len);
    }
    // Finished
    if (tls_read_hs_msg(c, hb, 25000) < 0) {
        if (c->err[0] == 0) snprintf(c->err, sizeof(c->err), "recv finished");
        goto fail;
    }
    if (hb.data[0] != TLS_HS_FINISHED) { snprintf(c->err, sizeof(c->err), "expected finished, got %d", hb.data[0]); goto fail; }
    {
        uint8_t fin_key[32];
        hkdf_expand_label(c->s_hs_sec, "finished", 0, 0, fin_key, 32);
        uint8_t th2[32];
        th_hash(c, th2);
        uint8_t expect[32];
        crypto::hmac_sha256(fin_key, 32, th2, 32, expect);
        if (hb.want != 32 || memcmp(expect, hb.data + 4, 32) != 0) {
            snprintf(c->err, sizeof(c->err), "finished verify failed");
            goto fail;
        }
    }
    th_append(c, hb.data, hb.len);

    // ---- master secret + app traffic ----
    uint8_t derived2[32];
    hkdf_expand_label(hs_secret, "derived", d0, 32, derived2, 32);
    uint8_t master[32];
        hkdf_extract(derived2, 32, z32, 32, master);   // IKM = Hash.length zeros
    derive_secret(master, "c ap traffic", c, c->c_ap_sec);
    derive_secret(master, "s ap traffic", c, c->s_ap_sec);
    derive_traffic_keys(c, c->c_ap_sec, c->c_ap_key, c->c_ap_iv);
    derive_traffic_keys(c, c->s_ap_sec, c->s_ap_key, c->s_ap_iv);

    // ---- client Finished (encrypted with hs keys) ----
    {
        uint8_t fin_key[32];
        hkdf_expand_label(c->c_hs_sec, "finished", 0, 0, fin_key, 32);
        uint8_t th2[32];
        th_hash(c, th2);
        uint8_t vd[32];
        crypto::hmac_sha256(fin_key, 32, th2, 32, vd);
        uint8_t msg[4 + 32];
        msg[0] = TLS_HS_FINISHED;
        { uint8_t* q = msg + 1; put24(q, 32); }
        memcpy(msg + 4, vd, 32);
        if (tls_send_encrypted_rec(c, c->c_hs_key, c->c_hs_iv, &c->hs_send_seq, msg, 36, TLS_RT_HANDSHAKE) < 0) {
            snprintf(c->err, sizeof(c->err), "send finished"); goto fail;
        }
        th_append(c, msg, 36);
    }

    return c;

fail:
    if (err && errsz > 0) {
        int n = (int)strlen(c->err);
        if (n == 0) n = 0;
        snprintf(err, (size_t)errsz, "%s", c->err[0] ? c->err : "tls handshake failed");
    }
    delete c;
    return 0;
}

int tls13_send(Tls13Conn* c, const uint8_t* data, int len) {
    int off = 0;
    while (off < len) {
        int n = len - off;
        if (n > 16384) n = 16384;
        if (tls_send_encrypted_rec(c, c->c_ap_key, c->c_ap_iv, &c->ap_send_seq, data + off, n, TLS_RT_APP) < 0)
            return -1;
        off += n;
    }
    return len;
}

int tls13_recv(Tls13Conn* c, uint8_t* buf, int maxlen, int timeout_ms) {
    // serve from leftover decrypted stream first
    if (c->dec_len - c->dec_pos > 0) {
        int n = c->dec_len - c->dec_pos;
        if (n > maxlen) n = maxlen;
        memcpy(buf, c->dec + c->dec_pos, (size_t)n);
        c->dec_pos += n;
        return n;
    }
    int type; uint8_t* pl; int plen;
    if (tls_read_record(c, &type, &pl, &plen, timeout_ms) < 0) return -1;
    if (type != TLS_RT_APP) return -1;
    uint8_t plain[16384 + 256];
    int olen;
    int ct = tls_decrypt_into(c, c->s_ap_key, c->s_ap_iv, c->ap_recv_seq++, pl, plen, plain, &olen);
    if (ct < 0) return -1;
    if (ct == TLS_RT_APP) {
        int n = olen;
        if (n > maxlen) n = maxlen;
        memcpy(buf, plain, (size_t)n);
        if (n < olen) {
            // stash remainder
            if (olen - n <= (int)sizeof(c->dec)) {
                memcpy(c->dec, plain + n, (size_t)(olen - n));
                c->dec_len = olen - n; c->dec_pos = 0;
            }
        }
        return n;
    }
    // handshake inside app record (e.g. NewSessionTicket): skip, retry
    return tls13_recv(c, buf, maxlen, timeout_ms);
}

void tls13_close(Tls13Conn* c) {
    if (!c) return;
    delete c;
}

const char* tls13_alpn(Tls13Conn* c) {
    if (!c) return "";
    return c->alpn[0] ? c->alpn : "";
}

} // namespace tls
} // namespace nefu
