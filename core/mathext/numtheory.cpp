#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 数论模块实现
#include "numtheory.h"
#include "complex.h"   // d_close（整型比较用 < 即可，这里仅做通用头依赖）
#include <cstddef>

namespace nefu {
namespace mathext {

// ---------------- GCD / LCM ----------------
i64 gcd(i64 a, i64 b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { i64 t = a % b; a = b; b = t; }
    return a;
}

i64 lcm(i64 a, i64 b) {
    i64 g = gcd(a, b);
    return (g == 0) ? 0 : (a / g) * b;
}

// 扩展 Euclid：递归版。返回 g = gcd(a,b)，并令 a*x + b*y = g。
i64 egcd(i64 a, i64 b, i64* x, i64* y) {
    if (b == 0) { *x = 1; *y = 0; return a; }
    i64 x1, y1;
    i64 g = egcd(b, a % b, &x1, &y1);
    *x = y1;
    *y = x1 - (a / b) * y1;
    return g;
}

// ---------------- 模幂 ----------------
i64 mod_pow(i64 base, i64 exp, i64 mod) {
    if (mod == 1) return 0;
    i64 result = 1 % mod;
    base %= mod;
    if (base < 0) base += mod;
    while (exp > 0) {
        if (exp & 1) result = (i64)((__int128)result * base % mod);
        base = (i64)((__int128)base * base % mod);
        exp >>= 1;
    }
    return result;
}

// ---------------- 模逆元 ----------------
i64 mod_inv(i64 a, i64 m) {
    i64 x, y;
    i64 g = egcd(a, m, &x, &y);
    if (g != 1) return -1;            // 不存在
    i64 r = x % m;
    if (r < 0) r += m;
    return r;
}

// ---------------- Euler phi ----------------
i64 euler_phi(i64 n) {
    if (n <= 0) return 0;
    i64 result = n;
    i64 nn = n;
    for (i64 p = 2; p * p <= nn; p++) {
        if (nn % p == 0) {
            while (nn % p == 0) nn /= p;
            result -= result / p;     // result *= (1 - 1/p)
        }
    }
    if (nn > 1) result -= result / nn;
    return result;
}

// ---------------- Möbius ----------------
int mobius(i64 n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int cnt = 0;
    for (i64 p = 2; p * p <= n; p++) {
        if (n % p == 0) {
            n /= p;
            if (n % p == 0) return 0;   // 有平方因子
            cnt++;
        }
    }
    if (n > 1) cnt++;                    // 剩一个大素数
    return (cnt & 1) ? -1 : 1;
}

// ---------------- 中国剩余定理 ----------------
i64 crt(const i64* r, const i64* m, int k) {
    i64 M = 1;
    for (int i = 0; i < k; i++) M *= m[i];
    i64 x = 0;
    for (int i = 0; i < k; i++) {
        i64 Mi = M / m[i];
        i64 inv = mod_inv(Mi % m[i], m[i]);
        if (inv < 0) return -1;
        x = (x + (i64)((__int128)r[i] * Mi % M) * inv) % M;
    }
    if (x < 0) x += M;
    return x;
}

// ---------------- Miller-Rabin 素性检验 ----------------
// 对小整数（n < 3.3e24），基 {2,3,5,7,11,13,17,19,23,29,31,37} 足够确定性。
static bool mr_test(i64 n, i64 a) {
    if (n % a == 0) return n == a;
    // n-1 = d * 2^s
    i64 d = n - 1;
    int s = 0;
    while (d % 2 == 0) { d /= 2; s++; }
    i64 x = mod_pow(a, d, n);
    if (x == 1 || x == n - 1) return true;
    for (int r = 1; r < s; r++) {
        x = (i64)((__int128)x * x % n);
        if (x == n - 1) return true;
        if (x == 1) return false;        // 提前退出：合数
    }
    return false;
}

bool is_prime(i64 n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0) return n == 2;
    static const i64 bases[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (int i = 0; i < 12; i++) {
        i64 a = bases[i];
        if (a >= n) continue;
        if (!mr_test(n, a)) return false;
    }
    return true;
}

// ---------------- 试除分解 ----------------
int factorize(i64 n, i64* out, int max_out) {
    if (n < 0) n = -n;
    int cnt = 0;
    for (i64 p = 2; p * p <= n && cnt < max_out; p++) {
        if (n % p == 0) {
            out[cnt++] = p;
            while (n % p == 0) n /= p;
        }
    }
    if (n > 1 && cnt < max_out) out[cnt++] = n;
    return cnt;
}

// ---------------- Pollard rho（简化） ----------------
i64 pollard_rho(i64 n) {
    if (n % 2 == 0) return 2;
    i64 x = 2, y = 2, d = 1;
    i64 c = 1;
    auto f = [&](i64 v) { return (i64)((__int128)v * v % n + c) % n; };
    while (d == 1) {
        x = f(x);
        y = f(f(y));
        d = gcd(x > y ? x - y : y - x, n);
    }
    return d;                            // 可能返回 n 本身（失败）
}

// ---------------- 线性同余 ----------------
i64 solve_lin_cong(i64 a, i64 b, i64 m) {
    i64 x, y;
    i64 g = egcd(a, m, &x, &y);
    if (b % g != 0) return -1;          // 无解
    i64 mod = m / g;
    i64 sol = (i64)((__int128)(b / g) * x % mod);
    sol %= mod;
    if (sol < 0) sol += mod;
    return sol;
}

// ---------------- 离散对数（BSGS） ----------------
i64 discrete_log(i64 g, i64 h, i64 p) {
    i64 m = 1;
    while (m * m < p) m++;               // m = ceil(sqrt(p))
    // baby 表：g^j -> j（小哈希表，用朴素线性查找，规模 m<=~1e6 时可接受）
    // 这里为了不依赖 STL，用两个并行数组做线性表（教学实现）。
    i64* baby_key = new i64[(size_t)m];
    i64* baby_val = new i64[(size_t)m];
    i64 cur = 1;
    for (i64 j = 0; j < m; j++) {
        baby_key[j] = cur;
        baby_val[j] = j;
        cur = (i64)((__int128)cur * g % p);
    }
    i64 gm_inv = mod_inv(mod_pow(g, m, p), p);
    i64 gamma = h;
    for (i64 i = 0; i < m; i++) {
        for (i64 j = 0; j < m; j++) {
            if (baby_key[j] == gamma) {
                i64 x = i * m + j;
                delete[] baby_key; delete[] baby_val;
                return x;
            }
        }
        gamma = (i64)((__int128)gamma * gm_inv % p);
    }
    delete[] baby_key; delete[] baby_val;
    return -1;
}

// ---------------- Jacobi 符号 ----------------
int jacobi_symbol(i64 a, i64 n) {
    if (n <= 0 || (n % 2 == 0)) return 0;
    a %= n;
    if (a < 0) a += n;
    int result = 1;
    while (a != 0) {
        while (a % 2 == 0) {
            a /= 2;
            i64 r = n % 8;
            if (r == 3 || r == 5) result = -result;
        }
        i64 t = a; a = n; n = t;
        if (a % 4 == 3 && n % 4 == 3) result = -result;
        a %= n;
    }
    return (n == 1) ? result : 0;
}

// ---------------- 下一个素数 ----------------
i64 next_prime(i64 n) {
    if (n < 2) return 2;
    i64 c = n + 1;
    if (c % 2 == 0) c++;
    while (!is_prime(c)) c += 2;
    return c;
}

// ---------------- 原根判定 ----------------
bool is_primitive_root(i64 g, i64 p) {
    if (p < 2) return false;
    i64 phi = p - 1;
    // phi 的所有素因子 q，检查 g^(phi/q) != 1 mod p
    i64 f[32]; int fc = factorize(phi, f, 32);
    for (int i = 0; i < fc; i++) {
        if (mod_pow(g, phi / f[i], p) == 1) return false;
    }
    return true;
}

// ---------------- 模平方根（Tonelli-Shanks 简化，p 为素数） ----------------
i64 mod_sqrt(i64 a, i64 p) {
    a %= p;
    if (a < 0) a += p;
    if (a == 0) return 0;
    if (p == 2) return a;
    if (mod_pow(a, (p - 1) / 2, p) != 1) return -1;   // 非二次剩余
    if (p % 4 == 3) return mod_pow(a, (p + 1) / 4, p);
    // 一般情形简化：试 1..p（仅教学，小 p 用）
    for (i64 x = 1; x < p; x++) if ((x * x) % p == a) return x;
    return -1;
}
// ---------------- 埃氏筛 ----------------
int prime_sieve(i64 N, i64* out, int out_cap) {
    if (N < 2) return 0;
    int n = (int)N;
    bool* comp = new bool[(size_t)(n + 1)]();
    for (int i = 2; (long long)i * i <= n; i++) {
        if (!comp[i]) {
            for (int j = i * i; j <= n; j += i) comp[j] = true;
        }
    }
    int cnt = 0;
    for (int i = 2; i <= n && cnt < out_cap; i++) {
        if (!comp[i]) out[cnt++] = i;
    }
    delete[] comp;
    return cnt;
}

i64 prime_pi(i64 N) {
    if (N < 2) return 0;
    i64 buf[1024];
    int c = prime_sieve(N, buf, 1024);
    return c;
}
// ---------------- 自检 ----------------
int numtheory_self_test() {
    int fails = 0;

    // 1. gcd(48,18)=6
    if (gcd(48, 18) != 6) fails++;
    // 2. egcd：48x + 18y = 6
    i64 x, y;
    i64 g = egcd(48, 18, &x, &y);
    if (g != 6) fails++;
    if (48 * x + 18 * y != 6) fails++;

    // 3. mod_pow(2,10,1000)=24
    if (mod_pow(2, 10, 1000) != 24) fails++;
    // 4. mod_inv(3,11)=4（3*4=12≡1 mod 11）
    if (mod_inv(3, 11) != 4) fails++;

    // 5. phi(10)=4（1,3,7,9）
    if (euler_phi(10) != 4) fails++;
    // 6. mobius(30)=-1（3 个不同素因子），mobius(4)=0
    if (mobius(30) != -1) fails++;
    if (mobius(4) != 0) fails++;

    // 7. Miller-Rabin：997 素数，561 合数（Carmichael）
    if (!is_prime(997)) fails++;
    if (is_prime(561)) fails++;

    // 8. CRT：x≡2(mod3), x≡3(mod5), x≡2(mod7) -> 23
    i64 r[3] = {2, 3, 2};
    i64 mm[3] = {3, 5, 7};
    if (crt(r, mm, 3) != 23) fails++;

    // 9. 分解：12 = {2,3}
    i64 f[8];
    int fc = factorize(12, f, 8);
    if (fc != 2 || f[0] != 2 || f[1] != 3) fails++;

    // 10. 离散对数：2^3=8 mod 13
    if (discrete_log(2, 8, 13) != 3) fails++;

    // 11. 线性同余：3x ≡ 2 (mod 7) -> x ≡ 3 (3*3=9≡2)
    if (solve_lin_cong(3, 2, 7) != 3) fails++;

    return fails;
}

} // namespace mathext
} // namespace nefu
