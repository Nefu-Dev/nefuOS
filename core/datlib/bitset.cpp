// nefuOS data-types library — bitset self test & helpers
#include "bitset.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 埃氏筛（素数筛）：把 2..n 的素数写入 out，返回素数个数。
// 经典 O(n log log n) 算法，用 bitset 标记合数，内存紧凑。
int bitset_eratosthenes(int n, int* out) {
    if (n < 2) return 0;
    bitset comp(n + 1);             // comp[i] = true 表示 i 是合数
    for (int i = 2; i * i <= n; i++) {
        if (!comp.test(i)) {
            for (int j = i * i; j <= n; j += i) comp.set(j);
        }
    }
    int c = 0;
    for (int i = 2; i <= n; i++) if (!comp.test(i)) out[c++] = i;
    return c;
}

// 二进制打印到字符串（高位在前）；buf 需 >= nbits+1
void bitset_to_string(const bitset& b, char* buf, int bufn) {
    int n = b.size();
    if (bufn <= 0) return;
    int k = 0;
    for (int i = n - 1; i >= 0 && k < bufn - 1; i--) buf[k++] = b.test(i) ? '1' : '0';
    buf[k] = 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int bitset_self_test() {
    g_fails = 0;
    {
        bitset b(70);                       // 70 位（2 个字 + 尾部 6 位）
        expect("bs-empty", b.none() && b.count() == 0);
        b.set(0); b.set(63); b.set(69);
        expect("bs-set", b.test(0) && b.test(63) && b.test(69) && !b.test(1));
        expect("bs-count", b.count() == 3);
        expect("bs-any", b.any());
        expect("bs-first", b.first_set() == 0);
        b.clear(0);
        expect("bs-first2", b.first_set() == 63);
        b.flip(63);
        expect("bs-flip", !b.test(63));
        b.flip(64); b.flip(65);
        expect("bs-flip2", b.test(64) && b.test(65));
        // 区间统计
        expect("bs-range", b.range_count(64, 69) == 3);
        // 全置位（含尾部掩码）
        b.set_all();
        expect("bs-all", b.all() && b.count() == 70);
        b.reset_all();
        expect("bs-reset", b.none());
        // 位运算
        bitset a1(16), a2(16);
        a1.set(1); a1.set(3); a2.set(3); a2.set(5);
        a1.bitwise_and(a2);
        expect("bs-and", a1.count() == 1 && a1.test(3));
        a1.bitwise_or(a2);
        expect("bs-or", a1.test(3) && a1.test(5));
        a1.bitwise_xor(a2);
        expect("bs-xor", a1.none());
        a1.flip_all();
        expect("bs-flipall", a1.test(1) && a1.test(3) && a1.count() == 16);
        // 字节导出
        bitset c(10);
        c.set(0); c.set(9);
        uint8_t bytes[2];
        c.to_bytes(bytes, 2);
        expect("bs-bytes", bytes[0] == 0x01 && bytes[1] == 0x02);
        // 字符串（高位在前）：位5..0 = 0 0 0 1 0 1
        bitset d(6);
        d.set(0); d.set(2);
        char buf[16];
        bitset_to_string(d, buf, sizeof(buf));
        expect("bs-str", buf[0] == '0' && buf[3] == '1' && buf[5] == '1' && buf[6] == 0);
    }
    {
        // 埃氏筛
        int primes[256];
        int n = bitset_eratosthenes(30, primes);
        expect("bs-sieve-n", n == 10);
        int exp[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
        bool ok = (n == 10);
        for (int i = 0; i < n && i < 10; i++) if (primes[i] != exp[i]) ok = false;
        expect("bs-sieve-v", ok);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
