// nefuOS mathlib —— 大整数 bigint
// 教学版：十进制字符串存储，手写加减乘除，适合理解逐位运算。
// 支持：加、减、乘、除（整除）、比较、取负、绝对值。
#pragma once
#include <string>
#include <cstdint>

namespace nefu {
namespace mathx {

// 十进制大整数（最多 2048 位，教学版定长数组）
struct BigInt {
    static const int CAP = 2048;
    int   len;                 // 有效位数
    int   sign;                // 1 正 / -1 负 / 0 零
    uint8_t d[CAP];            // 低位在前：d[0] 是个位

    BigInt();                                  // 0
    BigInt(int v);                             // 从 int 构造
    explicit BigInt(const char* s);            // 从十进制字符串构造
    explicit BigInt(const std::string& s) : BigInt(s.c_str()) {}

    // 加减乘除：结果写入本对象
    BigInt& add(const BigInt& b);              // this = this + b
    BigInt& sub(const BigInt& b);              // this = this - b
    BigInt& mul(const BigInt& b);              // this = this * b（逐位乘 + 累加）
    BigInt& div(const BigInt& b);              // this = this / b（长除法）
    BigInt& mod(const BigInt& b);              // this = this % b

    int cmp(const BigInt& b) const;            // 比较：负/0/正
    bool is_zero() const { return sign == 0; }
    std::string to_string() const;             // 转十进制字符串
    void set_zero();                           // 置零
    void trim_zeros();                         // 去掉高位零（内部使用）

    static BigInt pow10(int e);                // 10^e（教学演示）
    static BigInt factorial(int n);            // n!（教学演示）
};

// 便捷自由函数
inline BigInt operator+(const BigInt& a, const BigInt& b) { BigInt r = a; return r.add(b); }
inline BigInt operator-(const BigInt& a, const BigInt& b) { BigInt r = a; return r.sub(b); }
inline BigInt operator*(const BigInt& a, const BigInt& b) { BigInt r = a; return r.mul(b); }
inline BigInt operator/(const BigInt& a, const BigInt& b) { BigInt r = a; return r.div(b); }
inline BigInt operator%(const BigInt& a, const BigInt& b) { BigInt r = a; return r.mod(b); }
inline bool operator==(const BigInt& a, const BigInt& b) { return a.cmp(b) == 0; }
inline bool operator!=(const BigInt& a, const BigInt& b) { return a.cmp(b) != 0; }
inline bool operator<(const BigInt& a, const BigInt& b)  { return a.cmp(b) < 0; }
inline bool operator>(const BigInt& a, const BigInt& b)  { return a.cmp(b) > 0; }

int bigint_self_test();

} // namespace mathx
} // namespace nefu
