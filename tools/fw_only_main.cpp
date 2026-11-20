/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
#include "datlib/fenwick.h"
#include <cstdio>
int main() {
    int f = nefu::dt::fenwick_self_test();
    printf("fenwick=%d\n", f);
    return 0;
}

