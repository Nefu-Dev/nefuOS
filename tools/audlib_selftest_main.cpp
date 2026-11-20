/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS audlib 汇总自测宿主
#include <cstdio>
#include "audlib/aud_all.h"

int main() {
    int t = nefu::audx::aud_all_self_test();
    printf("audlib self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}

