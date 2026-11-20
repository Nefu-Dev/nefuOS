/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// cryptlib standalone self-test host
#include "cryptlib/crypt_all.h"
#include <cstdio>

int main() {
    int f = nefu::crypt::crypt_all_self_test();
    printf("cryptlib TOTAL=%d\n", f);
    return f;
}

