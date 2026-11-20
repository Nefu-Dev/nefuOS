/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
#include "gfxlib/geo.h"
#include <cstdio>
int main() { int f = nefu::gfxlib::geo_self_test(); printf("geo_self_test=%d\n", f); return 0; }

