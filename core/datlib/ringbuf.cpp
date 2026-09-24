// nefuOS data-types library — ringbuf self test & helpers
#include "ringbuf.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 把数组内容按 FIFO 顺序转储到 out（最大 n 个），返回实际个数
int ringbuf_dump(const ringbuf<int>& r, int* out, int n) {
    int k = r.size() < n ? r.size() : n;
    for (int i = 0; i < k; i++) out[i] = r.at(i);
    return k;
}

// 滑动窗口最大值：对每个长度为 w 的窗口输出最大值（双端队列经典题）
// 返回窗口个数；结果写入 out（长度 = n - w + 1）
int ringbuf_sliding_max(const int* a, int n, int w, int* out) {
    if (w <= 0 || n < w) return 0;
    // 用 ringbuf 存下标，保持下标递增且值递减（单调队列）
    ringbuf<int> q(n);
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        // 移除队首已滑出窗口的下标
        while (!q.empty()) {
            int f; q.peek(f);
            if (f <= i - w) q.pop(f); else break;
        }
        // 移除队尾值 <= a[i] 的下标（它们不可能再成为最大值）
        while (!q.empty()) {
            int b = q.at(q.size() - 1);
            if (a[b] <= a[i]) { int t; q.pop(t); if (q.size() == 0) break; }
            else break;
        }
        q.push(i);
        if (i >= w - 1) {
            int f; q.peek(f);
            out[cnt++] = a[f];
        }
    }
    return cnt;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int ringbuf_self_test() {
    g_fails = 0;
    {
        ringbuf<int> r(4);            // 容量 4，最多存 3 个
        expect("rb-empty", r.empty() && r.size() == 0);
        expect("rb-push", r.push(1) && r.push(2) && r.push(3));
        expect("rb-full", r.full());
        expect("rb-push-full", !r.push(4));       // 满
        int v = 0;
        expect("rb-peek", r.peek(v) && v == 1);
        expect("rb-pop", r.pop(v) && v == 1);
        expect("rb-pop2", r.pop(v) && v == 2);
        r.push(9); r.push(8);
        expect("rb-size", r.size() == 3);
        // FIFO 顺序：3, 9, 8
        int d[4]; int n = ringbuf_dump(r, d, 4);
        expect("rb-dump", n == 3 && d[0] == 3 && d[1] == 9 && d[2] == 8);
        // 覆盖式入队
        r.push_overwrite(7);
        expect("rb-overwrite", r.size() == 3);
        expect("rb-overwrite2", r.at(0) == 9);
        r.clear();
        expect("rb-clear", r.empty());
    }
    {
        // 滑动窗口最大值
        int a[] = {1, 3, -1, -3, 5, 3, 6, 7};
        int out[16];
        int n = ringbuf_sliding_max(a, 8, 3, out);
        expect("rb-slide-n", n == 6);
        expect("rb-slide-v", out[0] == 3 && out[1] == 3 && out[2] == 5 &&
                             out[3] == 5 && out[4] == 6 && out[5] == 7);
        int b[] = {5, 4, 3, 2, 1};
        int out2[8];
        int n2 = ringbuf_sliding_max(b, 5, 2, out2);
        expect("rb-slide-desc", n2 == 4 && out2[0] == 5 && out2[3] == 2);
        // 窗口等于数组长
        int out3[4];
        int n3 = ringbuf_sliding_max(a, 8, 8, out3);
        expect("rb-slide-full", n3 == 1 && out3[0] == 7);
    }
    {
        // 跨环回绕验证：容量 3（存 2），压 6 个
        ringbuf<int> r(3);
        int expect_seq[] = {9, 9, 9};
        for (int i = 0; i < 6; i++) {
            r.push(i);
            int v;
            r.pop(v);
        }
        expect("rb-wrap", r.empty());
        r.push(1); r.push(2); r.pop(expect_seq[0]);
        r.push(3); r.push(4);
        expect("rb-wrap2", r.size() == 2 && r.at(0) == 2 && r.at(1) == 3);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
