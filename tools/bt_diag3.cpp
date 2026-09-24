#include "datlib/btree.h"
#include <cstdio>
int main() {
    nefu::dt::btree t;
    for (int i = 0; i < 8; i++) t.insert(i, i * 2);
    printf("after insert 0..7:\n");
    t.debug_dump();
    for (int d = 0; d <= 6; d++) {
        printf("remove %d:\n", d);
        bool r = t.remove(d);
        printf("  ok=%d\n", r);
        t.debug_dump();
    }
    return 0;
}
