#include "datlib/fenwick.h"
#include <cstdio>
int main() {
    int a[] = {3, 1, 4, 1, 5, 9, 2, 6};
    nefu::dt::fenwick fw;
    fw.build(a, 8);
    printf("p0=%d p7=%d r25=%d\n", fw.prefix_sum(0), fw.prefix_sum(7), fw.range_sum(2, 5));
    fflush(stdout);
    fw.add(3, 10);
    fw.set(0, 100);
    printf("point3=%d total=%d\n", fw.point(3), fw.total());
    fflush(stdout);
    int ones[16];
    for (int i = 0; i < 16; i++) ones[i] = 1;
    nefu::dt::fenwick f2;
    f2.build(ones, 16);
    printf("lb5=%d lb1=%d lb16=%d\n", f2.lower_bound(5), f2.lower_bound(1), f2.lower_bound(16));
    fflush(stdout);
    printf("DONE\n");
    return 0;
}
