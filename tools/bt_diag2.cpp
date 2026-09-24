#include "datlib/btree.h"
#include <cstdio>

// 层序打印 B 树（临时调试用，访问 btree 私有成员不可行 → 用 inorder 分块推断）
// 改为：删除序列逐步打印 inorder，观察键的丢失/错位
int main() {
    nefu::dt::btree t;
    for (int i = 0; i < 30; i++) t.insert(i, i * 2);
    int ks[64], vs[64];
    int n = t.inorder(ks, vs, 64);
    printf("init(%d):", n);
    for (int i = 0; i < n; i++) printf(" %d", ks[i]);
    printf("\n");
    for (int d = 0; d <= 29; d++) {
        bool r = t.remove(d);
        n = t.inorder(ks, vs, 64);
        printf("del %d -> %s remain(%d):", d, r ? "ok" : "FAIL", n);
        for (int i = 0; i < n; i++) printf(" %d", ks[i]);
        printf("\n");
        if (n == 0) break;
    }
    printf("empty=%d\n", t.empty());
    return 0;
}
