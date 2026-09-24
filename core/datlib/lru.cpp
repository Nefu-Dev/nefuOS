// nefuOS data-types library — lru implementation & helpers
#include "lru.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 简易页面置换模拟：返回缺页次数。
// 页面号流 pages[0..n-1]，物理帧数 frames。
int lru_page_faults(const int* pages, int n, int frames) {
    lru c(frames);
    int faults = 0;
    for (int i = 0; i < n; i++) {
        int v, ev;
        if (c.get(pages[i], v)) {
            // 命中：内部已提升
        } else {
            faults++;
            c.put(pages[i], i, ev);   // 淘汰最久未用
        }
    }
    return faults;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int lru_self_test() {
    g_fails = 0;
    {
        lru c(3);
        int ev = -1;
        expect("lru-empty", c.size() == 0);
        c.put(1, 10, ev);
        c.put(2, 20, ev);
        c.put(3, 30, ev);
        expect("lru-size", c.size() == 3);
        int v = 0;
        expect("lru-hit", c.get(1, v) && v == 10);
        // 访问 1 后，最久未用变成 2 → 插入 4 应淘汰 2
        c.put(4, 40, ev);
        expect("lru-evict", ev == 2);
        expect("lru-evict-gone", !c.contains(2));
        expect("lru-remain", c.contains(1) && c.contains(3) && c.contains(4));
        // 顺序：最近是 4（刚插入），之后 1（之前访问过），再 3
        int ks[4], vs[4];
        int n = c.dump(ks, vs, 4);
        expect("lru-order", n == 3 && ks[0] == 4 && ks[1] == 1 && ks[2] == 3);
        // 更新已有键
        c.put(1, 999, ev);
        expect("lru-update", c.get(1, v) && v == 999);
        // 更新后 1 变成最近
        n = c.dump(ks, vs, 4);
        expect("lru-update-order", ks[0] == 1);
        // 容量 1
        lru one(1);
        one.put(7, 1, ev);
        one.put(8, 2, ev);
        expect("lru-cap1", ev == 7 && one.size() == 1);
        expect("lru-cap1-v", one.get(8, v) && v == 2);
    }
    {
        // 经典页面置换：7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1
        int pages[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
        int faults3 = lru_page_faults(pages, 20, 3);
        // 教科书 LRU 3 帧 = 12 次缺页
        expect("lru-pf3", faults3 == 12);
        int faults4 = lru_page_faults(pages, 20, 4);
        // 4 帧 LRU 该序列 = 8 次缺页（10 是 FIFO 口径）
        expect("lru-pf4", faults4 == 8);
        int faults1 = lru_page_faults(pages, 20, 1);
        expect("lru-pf1", faults1 == 20);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
