// nefuOS arbitrary-precision integer library (unsigned/signed, base 2^32)
// Portable: no STL, no exceptions.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace bignum {

struct BigInt {
    bool neg;             // sign
    uint32_t* d;          // little-endian limbs
    int n;                // limb count
    int cap;

    BigInt();
    BigInt(long long v);
    BigInt(const BigInt& o);
    ~BigInt();
    BigInt& operator=(const BigInt& o);

    void ensure(int need);
    void trim();                     // drop leading zero limbs
    void from_i64(long long v);
    void from_string(const char* s, int base = 10);
    void to_string(char* out, int cap, int base = 10) const;
    int  bitlen() const;             // bits in magnitude
    bool is_zero() const { return n == 0; }
    bool is_neg() const { return neg && n > 0; }

    void add_assign(const BigInt& o);   // same-sign magnitude add
    void sub_assign(const BigInt& o);   // |this| >= |o| required
    void mul_small(uint32_t m);
    void shl_bits(int k);
    void shr_bits(int k);
};

// arithmetic (values as signed)
BigInt big_add(const BigInt& a, const BigInt& b);
BigInt big_sub(const BigInt& a, const BigInt& b);
BigInt big_mul(const BigInt& a, const BigInt& b);
// q = a / b, r = a % b (both >= 0 assumed; returns false if b == 0)
bool big_divmod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);
BigInt big_pow(const BigInt& base, const BigInt& exp);
BigInt big_gcd(const BigInt& a, const BigInt& b);
BigInt big_mod_pow(const BigInt& base, const BigInt& exp, const BigInt& mod);
bool   big_is_prime(const BigInt& x, int rounds = 8);  // Miller-Rabin
BigInt big_abs(const BigInt& a);
int    big_cmp(const BigInt& a, const BigInt& b);

} // namespace bignum
} // namespace nefu
