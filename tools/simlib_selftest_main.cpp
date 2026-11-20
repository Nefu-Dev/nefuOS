/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS simlib 汇总自测宿主
#include <cstdio>
#include "simlib/sim_all.h"

int main() {
    int t = nefu::simx::sim_all_self_test();
    printf("simlib self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}

