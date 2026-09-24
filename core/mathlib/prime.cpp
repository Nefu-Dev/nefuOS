// nefuOS mathlib —— 素数实现 + 自测
#include "mathlib/prime.h"
#include <cmath>
#include <cstdio>

namespace nefu {
namespace mathx {

bool Prime::is_prime(int64_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (int64_t i = 5; i * i <= n; i += 6)
        if (n % i == 0 || n % (i + 2) == 0) return false;
    return true;
}

std::vector<int64_t> Prime::sieve(int limit) {
    std::vector<bool> mark(limit + 1, true);
    mark[0] = mark[1] = false;
    for (int i = 2; i * i <= limit; i++)
        if (mark[i])
            for (int j = i * i; j <= limit; j += i) mark[j] = false;
    std::vector<int64_t> r;
    for (int i = 2; i <= limit; i++) if (mark[i]) r.push_back(i);
    return r;
}

std::vector<std::pair<int64_t,int> > Prime::factors(int64_t n) {
    std::vector<std::pair<int64_t,int> > r;
    if (n < 2) return r;
    for (int64_t p = 2; p * p <= n; p++) {
        if (n % p == 0) {
            int e = 0;
            while (n % p == 0) { n /= p; e++; }
            r.push_back(std::pair<int64_t,int>(p, e));
        }
    }
    if (n > 1) r.push_back(std::pair<int64_t,int>(n, 1));
    return r;
}

int Prime::count_up_to(int n) {
    if (n < 2) return 0;
    return (int)sieve(n).size();
}

int64_t Prime::nth_prime(int k) {
    if (k < 1) return 0;
    int limit = 20;
    while ((int)sieve(limit).size() < k) limit *= 2;
    return sieve(limit)[k - 1];
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int prime_self_test() {
    g_fails = 0;
    {
        expect("prime-2", Prime::is_prime(2));
        expect("prime-3", Prime::is_prime(3));
        expect("prime-97", Prime::is_prime(97));
        expect("prime-not-4", !Prime::is_prime(4));
        expect("prime-not-100", !Prime::is_prime(100));
        expect("prime-1", !Prime::is_prime(1));
        expect("prime-0", !Prime::is_prime(0));
    }
    {
        std::vector<int64_t> p10 = Prime::sieve(10);
        expect("sieve-10", p10.size() == 4 && p10[0] == 2 && p10[3] == 7);
        std::vector<int64_t> p100 = Prime::sieve(100);
        expect("sieve-100", p100.size() == 25);
    }
    {
        std::vector<std::pair<int64_t,int> > f = Prime::factors(84);
        expect("factors-84", f.size() == 3 && f[0].first == 2 && f[0].second == 2 &&
               f[1].first == 3 && f[2].first == 7);
        std::vector<std::pair<int64_t,int> > fp = Prime::factors(97);
        expect("factors-prime", fp.size() == 1 && fp[0].first == 97);
    }
    {
        expect("pi-10", Prime::count_up_to(10) == 4);
        expect("pi-100", Prime::count_up_to(100) == 25);
        expect("nth-prime", Prime::nth_prime(1) == 2 && Prime::nth_prime(5) == 11);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
