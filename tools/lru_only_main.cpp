/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
#include "datlib/lru.h"
#include <cstdio>
int main(){ printf("start\n"); fflush(stdout); int f = nefu::dt::lru_self_test(); printf("lru=%d\n", f); return 0; }

