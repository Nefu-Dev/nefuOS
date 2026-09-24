// nefuOS arbitrary-precision integer library — implementation
#include "bigint.h"

namespace nefu {
namespace bignum {

BigInt::BigInt() : neg(false), d(0), n(0), cap(0) {}
BigInt::BigInt(long long v) : neg(false), d(0), n(0), cap(0) { from_i64(v); }
BigInt::BigInt(const BigInt& o) : neg(false), d(0), n(0), cap(0) { *this = o; }
BigInt::~BigInt() { if (d) delete[] d; }

BigInt& BigInt::operator=(const BigInt& o) {
    if (this != &o) {
        neg = o.neg;
        ensure(o.n);
        n = o.n;
        for (int i = 0; i < n; i++) d[i] = o.d[i];
    }
    return *this;
}

void BigInt::ensure(int need) {
    if (need <= cap) return;
    int nc = cap > 0 ? cap : 8;
    while (nc < need) nc *= 2;
    uint32_t* nd = new uint32_t[nc];
    if (!nd) { return; }
    for (int i = 0; i < n; i++) nd[i] = d[i];
    if (d) delete[] d;
    d = nd;
    cap = nc;
}

void BigInt::trim() {
    while (n > 0 && d[n - 1] == 0) n--;
}

void BigInt::from_i64(long long v) {
    n = 0; neg = false;
    unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
    if (v < 0) neg = true;
    ensure(2);
    if (u) {
        d[n++] = (uint32_t)(u & 0xFFFFFFFF);
        u >>= 32;
        if (u) d[n++] = (uint32_t)(u & 0xFFFFFFFF);
    }
}

void BigInt::from_string(const char* s, int base) {
    n = 0; neg = false;
    if (!s) return;
    const char* p = s;
    if (*p == '-') { neg = true; p++; }
    else if (*p == '+') p++;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0' && p[1]) { base = 8; p++; }
        else base = 10;
    }
    for (; *p; p++) {
        int dig;
        char c = *p;
        if (c >= '0' && c <= '9') dig = c - '0';
        else if (c >= 'a' && c <= 'f') dig = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') dig = c - 'A' + 10;
        else break;
        if (dig >= base) break;
        mul_small((uint32_t)base);
        if (n == 0) ensure(1);
        if (dig) {
            ensure(n + 1);
            d[0] += (uint32_t)dig;
            // propagate carry
            for (int i = 0; i < n; i++) {
                if (d[i] >= (uint32_t)base) { d[i] -= (uint32_t)base; d[i + 1]++; }
                else break;
            }
            // handle overflow into new limb
            if (d[n] != 0) n++;
        }
        trim();
    }
    if (n == 0) neg = false;
}

void BigInt::to_string(char* out, int cap, int base) const {
    if (cap <= 1) { if (cap) out[0] = 0; return; }
    char tmp[1024];
    int idx = 0;
    BigInt t(*this);
    t.neg = false;
    if (t.n == 0) { out[0] = '0'; out[1] = 0; return; }
    while (t.n > 0 && idx < 1022) {
        // divide by base (single-limb divisor)
        uint64_t rem = 0;
        for (int i = t.n - 1; i >= 0; i--) {
            uint64_t cur = (rem << 32) | t.d[i];
            t.d[i] = (uint32_t)(cur / (uint64_t)base);
            rem = cur % (uint64_t)base;
        }
        t.trim();
        int dig = (int)rem;
        tmp[idx++] = (char)(dig < 10 ? '0' + dig : 'a' + (dig - 10));
    }
    int o = 0;
    if (neg && o + 1 < cap) out[o++] = '-';
    while (idx > 0 && o < cap - 1) out[o++] = tmp[--idx];
    out[o] = 0;
}

int BigInt::bitlen() const {
    if (n == 0) return 0;
    uint32_t top = d[n - 1];
    int bits = (n - 1) * 32;
    while (top) { bits++; top >>= 1; }
    return bits;
}

void BigInt::add_assign(const BigInt& o) {
    ensure(o.n + 1);
    uint64_t carry = 0;
    int m = n > o.n ? n : o.n;
    for (int i = 0; i < m; i++) {
        uint64_t a = i < n ? d[i] : 0;
        uint64_t b = i < o.n ? o.d[i] : 0;
        uint64_t s = a + b + carry;
        d[i] = (uint32_t)(s & 0xFFFFFFFF);
        carry = s >> 32;
    }
    if (carry) { d[m] = (uint32_t)carry; n = m + 1; }
    else if (m > n) n = m;
}

void BigInt::sub_assign(const BigInt& o) {
    // |this| >= |o|, same sign
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        uint64_t b = i < o.n ? o.d[i] : 0;
        uint64_t a = d[i];
        int64_t r = (int64_t)a - (int64_t)b - (int64_t)borrow;
        if (r < 0) { r += (1LL << 32); borrow = 1; }
        else borrow = 0;
        d[i] = (uint32_t)(r & 0xFFFFFFFF);
    }
    trim();
}

void BigInt::mul_small(uint32_t m) {
    if (m == 0) { n = 0; return; }
    ensure(n + 1);
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t cur = (uint64_t)d[i] * m + carry;
        d[i] = (uint32_t)(cur & 0xFFFFFFFF);
        carry = cur >> 32;
    }
    if (carry) { d[n++] = (uint32_t)carry; }
    trim();
}

void BigInt::shl_bits(int k) {
    if (n == 0 || k == 0) return;
    int limb_shift = k / 32;
    int bit_shift = k % 32;
    int old_n = n;
    ensure(n + limb_shift + 1);
    if (limb_shift) {
        for (int i = old_n - 1; i >= 0; i--) d[i + limb_shift] = d[i];
        for (int i = 0; i < limb_shift; i++) d[i] = 0;
        n = old_n + limb_shift;
    }
    if (bit_shift) {
        uint32_t carry = 0;
        for (int i = 0; i < n; i++) {
            uint32_t cur = d[i];
            d[i] = (cur << bit_shift) | carry;
            carry = cur >> (32 - bit_shift);
        }
        if (carry) { ensure(n + 1); d[n++] = carry; }
    }
    trim();
}

void BigInt::shr_bits(int k) {
    if (n == 0) return;
    int limb_shift = k / 32;
    int bit_shift = k % 32;
    if (limb_shift >= n) { n = 0; return; }
    if (limb_shift) {
        for (int i = 0; i + limb_shift < n; i++) d[i] = d[i + limb_shift];
        n -= limb_shift;
    }
    if (bit_shift) {
        uint32_t carry = 0;
        for (int i = n - 1; i >= 0; i--) {
            uint32_t cur = d[i];
            d[i] = (cur >> bit_shift) | carry;
            carry = cur << (32 - bit_shift);
        }
        trim();
    }
    trim();
}

// ---------------- free functions ----------------

int big_cmp(const BigInt& a, const BigInt& b) {
    // compare magnitudes (same sign assumed)
    if (a.n != b.n) return a.n < b.n ? -1 : 1;
    for (int i = a.n - 1; i >= 0; i--) {
        if (a.d[i] != b.d[i]) return a.d[i] < b.d[i] ? -1 : 1;
    }
    return 0;
}

BigInt big_abs(const BigInt& a) {
    BigInt r(a);
    r.neg = false;
    return r;
}

BigInt big_add(const BigInt& a, const BigInt& b) {
    BigInt r(a);
    if (r.neg == b.neg) {
        r.add_assign(b);
    } else {
        if (big_cmp(big_abs(a), big_abs(b)) >= 0) {
            r.sub_assign(b);
        } else {
            BigInt tmp(b);
            tmp.sub_assign(a);
            r = tmp;
        }
    }
    return r;
}

BigInt big_sub(const BigInt& a, const BigInt& b) {
    BigInt nb(b);
    nb.neg = !nb.neg;
    return big_add(a, nb);
}

BigInt big_mul(const BigInt& a, const BigInt& b) {
    BigInt r;
    if (a.n == 0 || b.n == 0) return r;
    r.ensure(a.n + b.n);
    r.n = a.n + b.n;
    for (int i = 0; i < r.n; i++) r.d[i] = 0;
    for (int i = 0; i < a.n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b.n; j++) {
            uint64_t cur = (uint64_t)a.d[i] * b.d[j] + r.d[i + j] + carry;
            r.d[i + j] = (uint32_t)(cur & 0xFFFFFFFF);
            carry = cur >> 32;
        }
        r.d[i + b.n] = (uint32_t)(r.d[i + b.n] + carry);
    }
    r.trim();
    r.neg = a.neg != b.neg;
    return r;
}

bool big_divmod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {
    if (b.n == 0) return false;
    q = BigInt();
    r = BigInt();
    r.ensure(a.n + 1);
    r.n = 0;
    // long division bit by bit (simple, O(n*bits))
    for (int bit = a.bitlen() - 1; bit >= 0; bit--) {
        // r = r << 1 | bit(a, bit)
        r.shl_bits(1);
        if (bit / 32 < a.n && ((a.d[bit / 32] >> (bit % 32)) & 1)) {
            r.ensure(r.n + 1);
            r.d[0] |= 1;
            if (r.n == 0) r.n = 1;
        }
        r.trim();
        if (big_cmp(r, b) >= 0) {
            r.sub_assign(b);
            q.shl_bits(1);
            q.ensure(q.n + 1);
            q.d[0] |= 1;
            if (q.n == 0) q.n = 1;
        } else {
            q.shl_bits(1);
        }
        q.trim();
    }
    q.neg = a.neg != b.neg;
    r.neg = a.neg;
    return true;
}

BigInt big_pow(const BigInt& base, const BigInt& exp) {
    BigInt result(1);
    BigInt b(base);
    BigInt e(exp);
    BigInt two(2);
    while (e.n > 0) {
        // if e odd: result *= b
        if (e.d[0] & 1) result = big_mul(result, b);
        e.shr_bits(1);
        if (e.n > 0) b = big_mul(b, b);
    }
    if (exp.neg) return BigInt();
    return result;
}

BigInt big_gcd(const BigInt& a, const BigInt& b) {
    BigInt x = big_abs(a), y = big_abs(b);
    BigInt q, r;
    while (y.n > 0) {
        big_divmod(x, y, q, r);
        x = y;
        y = r;
    }
    return x;
}

BigInt big_mod_pow(const BigInt& base, const BigInt& exp, const BigInt& mod) {
    if (mod.n == 0) return BigInt();
    BigInt result(1 % (mod.n ? mod.d[0] : 1));
    BigInt b(base);
    BigInt e(exp);
    BigInt q, r;
    while (e.n > 0) {
        if (e.d[0] & 1) {
            result = big_mul(result, b);
            big_divmod(result, mod, q, r);
            result = r;
        }
        e.shr_bits(1);
        if (e.n > 0) {
            b = big_mul(b, b);
            big_divmod(b, mod, q, r);
            b = r;
        }
    }
    return result;
}

// Miller-Rabin deterministic-ish primality test
bool big_is_prime(const BigInt& x, int rounds) {
    BigInt two(2), three(3);
    if (big_cmp(x, two) < 0) return false;
    if (big_cmp(x, two) == 0 || big_cmp(x, three) == 0) return true;
    if (!(x.d[0] & 1)) return false;
    // write x-1 = d * 2^s
    BigInt d = big_sub(x, BigInt(1));
    int s = 0;
    while (d.n > 0 && !(d.d[0] & 1)) { d.shr_bits(1); s++; }
    // small trial division first
    static const uint32_t small_primes[] = {2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97};
    for (size_t i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); i++) {
        if (big_cmp(x, BigInt((long long)small_primes[i])) == 0) return true;
        // x % p == 0 ?
        BigInt q, r;
        big_divmod(x, BigInt((long long)small_primes[i]), q, r);
        if (r.n == 0) return false;
    }
    // bases: deterministic set for < 2^64 plus small primes for larger
    static const uint64_t bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    BigInt xm1 = big_sub(x, BigInt(1));
    for (int rnd = 0; rnd < rounds; rnd++) {
        BigInt a((long long)bases[rnd % 12]);
        BigInt xd = big_mod_pow(a, d, x);
        if (big_cmp(xd, BigInt(1)) == 0 || big_cmp(xd, xm1) == 0) continue;
        bool composite = true;
        for (int i = 1; i < s; i++) {
            xd = big_mod_pow(xd, BigInt(2), x);
            if (big_cmp(xd, xm1) == 0) { composite = false; break; }
            if (big_cmp(xd, BigInt(1)) == 0) break;
        }
        if (composite) return false;
    }
    return true;
}

} // namespace bignum
} // namespace nefu
