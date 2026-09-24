// nefuOS simlib 汇总自测宿主
#include <cstdio>
#include "simlib/sim_all.h"

int main() {
    int t = nefu::simx::sim_all_self_test();
    printf("simlib self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}
