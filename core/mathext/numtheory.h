// nefuOS 数学扩展库 —— 数论模块
// 基于 long long 的整数数论：GCD/扩展 GCD、模逆元、Euler phi、Möbius、
// 中国剩余定理、模幂、Miller-Rabin 素性检验、试除/Pollard rho 分解、
// 线性同余、离散对数（BSGS）。
#pragma once

namespace nefu {
namespace mathext {

typedef long long i64;

// ---------------- GCD / LCM ----------------
i64 gcd(i64 a, i64 b);                          // 辗转相除
i64 lcm(i64 a, i64 b);
// 扩展 Euclid：返回 gcd，且 *x,*y 满足 a*x + b*y = gcd。
i64 egcd(i64 a, i64 b, i64* x, i64* y);

// ---------------- 模运算 ----------------
i64 mod_pow(i64 base, i64 exp, i64 mod);        // 快速模幂
i64 mod_inv(i64 a, i64 m);                      // 模逆元，不存在返回 -1

// ---------------- 数论函数 ----------------
i64 euler_phi(i64 n);                           // phi(n)：1..n 中与 n 互素个数
int mobius(i64 n);                               // μ(n)：无平方因子时 (-1)^k，否则 0

// ---------------- 中国剩余定理 ----------------
// 解 x ≡ r[i] (mod m[i])，moduli 两两互素。返回 x mod prod，失败返回 -1。
i64 crt(const i64* r, const i64* m, int k);

// ---------------- 素性 / 分解 ----------------
bool is_prime(i64 n);                           // Miller-Rabin（确定性小素数基）
// 试除法分解：把 n 的素因子依次写入 out（不计重数），返回个数。
int  factorize(i64 n, i64* out, int max_out);
// Pollard rho（简化）：找 n 的一个非平凡因子，失败返回 1。
i64 pollard_rho(i64 n);

// ---------------- 线性同余 / 离散对数 ----------------
// 解 a x ≡ b (mod m)：返回解 mod m；无解返回 -1。
i64 solve_lin_cong(i64 a, i64 b, i64 m);
// 离散对数（BSGS 简化）：求最小 x 使 g^x ≡ h (mod p)，无解返回 -1。
i64 discrete_log(i64 g, i64 h, i64 p);

// ---------------- 更多数论 ----------------
int  jacobi_symbol(i64 a, i64 n);          // Jacobi 符号（n 奇）
i64  next_prime(i64 n);                     // 找 n 之后的下一个素数
bool is_primitive_root(i64 g, i64 p);      // g 是否模 p 的原根
i64  mod_sqrt(i64 a, i64 p);                // 模 p 平方根（Tonelli 简化），无解 -1
// ---------------- 素数筛 ----------------
// 埃氏筛：返回 <=N 的素数列表到 out（容量 out_cap），返回个数。
int prime_sieve(i64 N, i64* out, int out_cap);
// 计算 [2,N] 内素数个数（用筛）。
i64 prime_pi(i64 N);
// ---------------- 自检 ----------------
// 已知值：
//   gcd(48,18)=6；egcd: 48*(-1)+18*3=6
//   561 是 Carmichael 数（合数）；997 是素数
// 返回失败条数。
int numtheory_self_test();

} // namespace mathext
} // namespace nefu
