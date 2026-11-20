/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS stdcxx - <cstdio>
#pragma once
#include "_core.h"
#include <stdio.h>
namespace nefu {
namespace stdx {
using ::printf; using ::sprintf; using ::snprintf; using ::vsnprintf;
using ::fopen; using ::fclose; using ::fread; using ::fwrite;
using ::fgetc; using ::fputc; using ::fgets; using ::fputs;
using ::puts; using ::putchar; using ::getchar; using ::fflush;
using ::fprintf; using ::vprintf; using ::fseek; using ::ftell; using ::rewind;
}}

