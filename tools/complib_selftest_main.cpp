// complib standalone self-test host
#include "complib/comp_all.h"
#include <cstdio>

int main() {
    int f = nefu::comp::comp_all_self_test();
    printf("complib TOTAL=%d\n", f);
    return f;
}
