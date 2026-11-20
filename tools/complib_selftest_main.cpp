/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// complib standalone self-test host
#include "complib/comp_all.h"
#include <cstdio>

int main() {
    int f = nefu::comp::comp_all_self_test();
    printf("complib TOTAL=%d\n", f);
    return f;
}

