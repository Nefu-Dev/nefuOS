/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS stdcxx - <cstdlib>
#pragma once
#include "_core.h"
#include <stdlib.h>
namespace nefu {
namespace stdx {
using ::malloc; using ::free; using ::calloc; using ::realloc;
using ::atoi; using ::atol; using ::atof; using ::strtol; using ::strtoul;
using ::abs; using ::labs; using ::rand; using ::srand; using ::qsort;
using ::exit; using ::abort; using ::system;
}}

