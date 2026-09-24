#include "datlib/fenwick.h"
#include <cstdio>
int main() {
    int f = nefu::dt::fenwick_self_test();
    printf("fenwick=%d\n", f);
    return 0;
}
