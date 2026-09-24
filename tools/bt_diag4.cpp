#include "datlib/btree.h"
#include <cstdio>
int main() {
    nefu::dt::btree t;
    for (int i = 0; i < 30; i++) t.insert(i, i * 2);
    for (int d = 0; d <= 23; d++) {
        bool r = t.remove(d);
        if (!r) printf("del %d FAIL\n", d);
    }
    printf("--- after del 0..23 ---\n");
    t.debug_dump();
    int ks[64], vs[64];
    int n = t.inorder(ks, vs, 64);
    printf("remain(%d):", n);
    for (int i = 0; i < n; i++) printf(" %d", ks[i]);
    printf("\n");
    for (int d = 24; d <= 29; d++) {
        bool r = t.remove(d);
        printf("del %d -> %s\n", d, r ? "ok" : "FAIL");
        if (!r) t.debug_dump();
    }
    return 0;
}
