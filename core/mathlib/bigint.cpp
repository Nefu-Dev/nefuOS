// nefuOS mathlib —— 大整数实现 + 自测
#include "mathlib/bigint.h"
#include <cstdio>
#include <cstring>

namespace nefu {
namespace mathx {

BigInt::BigInt() { set_zero(); }

BigInt::BigInt(int v) { set_zero(); sign = v > 0 ? 1 : (v < 0 ? -1 : 0); uint64_t a = v < 0 ? -(int64_t)v : (int64_t)v; len = 0; do { d[len++] = (uint8_t)(a % 10); a /= 10; } while (a > 0); if (len == 0) len = 1; }

BigInt::BigInt(const char* s) {
    set_zero();
    if (!s) return;
    const char* p = s;
    int sg = 1;
    if (*p == '-') { sg = -1; p++; }
    int n = (int)strlen(p);
    if (n == 0) return;
    len = n;
    for (int i = 0; i < n; i++) d[i] = (uint8_t)(p[n - 1 - i] - '0');
    sign = sg;
    trim_zeros();
}

void BigInt::set_zero() { len = 1; sign = 0; memset(d, 0, sizeof(d)); }

void BigInt::trim_zeros() { while (len > 1 && d[len - 1] == 0) len--; if (len == 1 && d[0] == 0) sign = 0; }

int BigInt::cmp(const BigInt& b) const {
    if (sign != b.sign) return sign < b.sign ? -1 : 1;
    if (sign == 0) return 0;
    if (len != b.len) return (len < b.len) ? -sign : sign;
    for (int i = len - 1; i >= 0; i--)
        if (d[i] != b.d[i]) return d[i] < b.d[i] ? -sign : sign;
    return 0;
}

std::string BigInt::to_string() const {
    std::string s;
    if (sign < 0) s += '-';
    if (sign == 0) return "0";
    for (int i = len - 1; i >= 0; i--) s += (char)('0' + d[i]);
    return s;
}

BigInt& BigInt::add(const BigInt& b) {
    if (sign == 0) { *this = b; return *this; }
    if (b.sign == 0) return *this;
    if (sign == b.sign) {
        int carry = 0;
        for (int i = 0; i < CAP; i++) {
            int s = d[i] + b.d[i] + carry;
            d[i] = (uint8_t)(s % 10);
            carry = s / 10;
        }
        len = CAP;
        trim_zeros();
        return *this;
    }
    // 异号：比较绝对值，符号取大者
    BigInt tmp = b;
    int sa = sign, sb = tmp.sign;
    sign = 1; tmp.sign = 1;
    int c = cmp(tmp);
    sign = sa; tmp.sign = sb;
    if (c >= 0) {
        // |this| >= |b|：this - b
        int borrow = 0;
        for (int i = 0; i < CAP; i++) {
            int v = d[i] - tmp.d[i] - borrow;
            if (v < 0) { v += 10; borrow = 1; } else borrow = 0;
            d[i] = (uint8_t)v;
        }
        trim_zeros();
        return *this;
    }
    // |this| < |b|：b - this，符号取 b
    int borrow = 0;
    for (int i = 0; i < CAP; i++) {
        int v = tmp.d[i] - d[i] - borrow;
        if (v < 0) { v += 10; borrow = 1; } else borrow = 0;
        d[i] = (uint8_t)v;
    }
    sign = tmp.sign;
    trim_zeros();
    return *this;
}

BigInt& BigInt::sub(const BigInt& b) {
    BigInt nb = b;
    nb.sign = -nb.sign;
    return add(nb);
}

BigInt& BigInt::mul(const BigInt& b) {
    if (is_zero() || b.is_zero()) { set_zero(); return *this; }
    int ns = sign * b.sign;
    uint16_t tmp[CAP];
    memset(tmp, 0, sizeof(tmp));
    for (int i = 0; i < len; i++)
        for (int j = 0; j < b.len && i + j < CAP; j++)
            tmp[i + j] += (uint16_t)d[i] * (uint16_t)b.d[j];
    int carry = 0;
    for (int i = 0; i < CAP; i++) {
        tmp[i] += (uint16_t)carry;
        d[i] = (uint8_t)(tmp[i] % 10);
        carry = tmp[i] / 10;
    }
    len = CAP;
    sign = ns;
    trim_zeros();
    return *this;
}

BigInt& BigInt::div(const BigInt& b) {
    if (b.is_zero()) { set_zero(); return *this; }  // 除零返回 0（教学容错）
    if (cmp(b) < 0) { set_zero(); return *this; }
    int ns = sign * b.sign;
    BigInt num = *this; num.sign = 1;
    BigInt den = b;     den.sign = 1;
    BigInt q;           // 商
    q.set_zero();
    // 长除法：从高位逐位试商
    BigInt rem;
    rem.len = 0;                      // 空余数：每次追加一位，rem = rem*10 + 位
    rem.sign = 1;
    memset(rem.d, 0, sizeof(rem.d));
    for (int i = num.len - 1; i >= 0; i--) {
        // rem = rem*10 + digit（低位在前：新数字追加到下标 len）
        if (rem.len >= CAP) return *this;   // 防御：超长
        // rem = rem*10 + 位：整体右移一格，新位放个位
        for (int k = rem.len; k > 0; k--) rem.d[k] = rem.d[k - 1];
        rem.d[0] = num.d[i];
        rem.len = rem.len + 1;
        rem.trim_zeros();
        // 试商 0..9
        int digit = 0;
        while (rem.cmp(den) >= 0) { rem.sub(den); digit++; }
        q.d[i] = (uint8_t)digit;
        if (q.len <= i) q.len = i + 1;
    }
    q.sign = ns;
    q.trim_zeros();
    *this = q;
    return *this;
}

BigInt& BigInt::mod(const BigInt& b) {
    if (b.is_zero()) { set_zero(); return *this; }
    BigInt q = *this; q.div(b);
    BigInt p = q; p.mul(b);
    sub(p);
    if (sign == 0) { }
    return *this;
}

BigInt BigInt::pow10(int e) {
    BigInt r = 1;
    for (int i = 0; i < e; i++) { r = r.mul(BigInt(10)); }
    return r;
}

BigInt BigInt::factorial(int n) {
    BigInt r = 1;
    for (int i = 2; i <= n; i++) r = r.mul(BigInt(i));
    return r;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int bigint_self_test() {
    g_fails = 0;
    {
        BigInt a(123), b(456);
        expect("bigint-add", (a + b).to_string() == "579");
        expect("bigint-sub", (b - a).to_string() == "333");
        expect("bigint-sub-neg", (a - b).to_string() == "-333");
        BigInt c("99999999999999999999"), d("1");
        expect("bigint-add-big", (c + d).to_string() == "100000000000000000000");
        expect("bigint-sub-big", (c - d).to_string() == "99999999999999999998");
    }
    {
        BigInt a(12), b(34);
        expect("bigint-mul", (a * b).to_string() == "408");
        BigInt c("123456789"), d("987654321");
        expect("bigint-mul-big", (c * d).to_string() == "121932631112635269");
    }
    {
        BigInt a(100), b(7);
        expect("bigint-div", (a / b).to_string() == "14");
        expect("bigint-mod", (a % b).to_string() == "2");
        BigInt c("12345678901234567890"), d("1000000007");
        expect("bigint-div-big", (c / d).to_string() == "12345678814");
    }
    {
        expect("bigint-fact5", BigInt::factorial(5).to_string() == "120");
        expect("bigint-fact10", BigInt::factorial(10).to_string() == "3628800");
        expect("bigint-pow10", BigInt::pow10(3).to_string() == "1000");
        expect("bigint-cmp", BigInt("999") < BigInt("1000"));
        expect("bigint-eq", BigInt(7) == BigInt("7"));
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
