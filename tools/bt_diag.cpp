#include "datlib/btree.h"
#include <cstdio>
int main() {
    nefu::dt::btree t;
    for (int i = 0; i < 30; i++) t.insert(i, i * 2);
    t.remove(0); t.remove(29); t.remove(15);
    for (int i = 1; i <= 29; i++) {
        bool r = t.remove(i);
        if (i != 15 && !r) printf("FAIL remove %d\n", i);
    }
    printf("empty=%d size=%d\n", t.empty(), t.size());
    int ks[64], vs[64];
    int n = t.inorder(ks, vs, 64);
    printf("remain=%d:", n);
    for (int i = 0; i < n; i++) printf(" %d", ks[i]);
    printf("\n");
    return 0;
}
