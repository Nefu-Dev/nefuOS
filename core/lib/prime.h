// nefuOS prime number library
// Sieve, trial division, factorization, next-prime. Portable.
#pragma once
#include <stdint.h>
#include "../klib/klib.h"

namespace nefu {
namespace prime {

// sieve of Eratosthenes up to limit; returns primes list (may be empty on OOM)
void sieve(int limit, List<int>& out);

bool is_prime_u32(uint32_t n);                  // 64-bit safe deterministic MR
bool is_prime_u64(uint64_t n);                  // deterministic for < 2^64
uint64_t next_prime_u64(uint64_t n);
int  count_primes_leq(int n);                   // via sieve (n <= 1,000,000)

// factorization: pairs (factor, exponent)
struct Factor {
    uint64_t p;
    int e;
    Factor() : p(0), e(0) {}
    Factor(uint64_t p_, int e_) : p(p_), e(e_) {}
};
void factorize_u64(uint64_t n, List<Factor>& out);

} // namespace prime
} // namespace nefu
