// nefuOS prime number library — implementation
#include "prime.h"

namespace nefu {
namespace prime {

void sieve(int limit, List<int>& out) {
    out.erase_all();
    if (limit < 2) return;
    int n = limit + 1;
    uint8_t* comp = new uint8_t[n];
    if (!comp) return;
    for (int i = 0; i < n; i++) comp[i] = 0;
    for (int i = 2; i * i <= limit; i++) {
        if (!comp[i]) {
            for (int j = i * i; j <= limit; j += i) comp[j] = 1;
        }
    }
    for (int i = 2; i <= limit; i++) {
        if (!comp[i]) out.push(i);
    }
    delete[] comp;
}

int count_primes_leq(int n) {
    if (n < 2) return 0;
    List<int> primes;
    sieve(n, primes);
    return primes.size();
}

// ---------- 64-bit Miller-Rabin (deterministic for < 2^64) ----------

static inline uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m) {
    // 64x64 -> 128 product via shifts (portable, no __int128)
    uint64_t r = 0;
    a %= m;
    while (b) {
        if (b & 1) {
            uint64_t x = r + a;
            if (x < r || x >= m) x -= m;
            r = x % m;
        }
        a = (a + a) % m;
        b >>= 1;
    }
    return r % m;
}

static inline uint64_t powmod_u64(uint64_t base, uint64_t exp, uint64_t m) {
    uint64_t r = 1 % m;
    base %= m;
    while (exp) {
        if (exp & 1) r = mulmod_u64(r, base, m);
        base = mulmod_u64(base, base, m);
        exp >>= 1;
    }
    return r;
}

static bool miller_rabin_u64(uint64_t n, uint64_t a) {
    // n odd > 2; n-1 = d * 2^s
    uint64_t d = n - 1;
    int s = 0;
    while ((d & 1) == 0) { d >>= 1; s++; }
    uint64_t x = powmod_u64(a % n, d, n);
    if (x == 1 || x == n - 1) return true;
    for (int i = 1; i < s; i++) {
        x = mulmod_u64(x, x, n);
        if (x == n - 1) return true;
        if (x == 1) return false;
    }
    return false;
}

bool is_prime_u64(uint64_t n) {
    if (n < 2) return false;
    static const uint64_t small[] = {2,3,5,7,11,13,17,19,23,29,31,37};
    for (size_t i = 0; i < sizeof(small) / sizeof(small[0]); i++) {
        if (n == small[i]) return true;
        if (n % small[i] == 0) return false;
    }
    // deterministic bases for n < 2^64
    static const uint64_t bases[] = {
        2, 325, 9375, 28178, 450775, 9780504, 1795265022ULL
    };
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        uint64_t a = bases[i] % n;
        if (a == 0) continue;
        if (!miller_rabin_u64(n, a)) return false;
    }
    return true;
}

bool is_prime_u32(uint32_t n) {
    return is_prime_u64(n);
}

uint64_t next_prime_u64(uint64_t n) {
    if (n <= 2) return 2;
    if (n % 2 == 0) n++;
    while (!is_prime_u64(n)) n += 2;
    return n;
}

void factorize_u64(uint64_t n, List<Factor>& out) {
    out.erase_all();
    if (n <= 1) return;
    for (uint64_t p = 2; p * p <= n && p <= 65536; p++) {
        if (n % p == 0) {
            int e = 0;
            while (n % p == 0) { n /= p; e++; }
            out.push(Factor(p, e));
        }
        if (p == 2) p = 1; // advance to odd after 2
    }
    if (n > 1) out.push(Factor(n, 1));
}

} // namespace prime
} // namespace nefu
