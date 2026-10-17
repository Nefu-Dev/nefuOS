// nefuOS stdcxx - aggregate header (C++ standard library for nefuCpp)
// usage: #include "stdcxx/stdcxx_all.h"
//
// Provides a self-contained C++ standard library subset for nefuCpp programs,
// implemented header-only on the nefuOS kernel primitives (kalloc/kfree).
// Namespace: nefu::stdx (a program can add "using namespace nefu::stdx;").
#pragma once

#include "stdcxx/cstddef.h"
#include "stdcxx/cstdint.h"
#include "stdcxx/utility.h"
#include "stdcxx/type_traits.h"
#include "stdcxx/limits.h"
#include "stdcxx/functional.h"
#include "stdcxx/memory.h"
#include "stdcxx/iterator.h"
#include "stdcxx/array.h"
#include "stdcxx/vector.h"
#include "stdcxx/list.h"
#include "stdcxx/string.h"
#include "stdcxx/map.h"
#include "stdcxx/set.h"
#include "stdcxx/queue.h"
#include "stdcxx/algorithm.h"
#include "stdcxx/numeric.h"
#include "stdcxx/cstring.h"
#include "stdcxx/cmath.h"
#include "stdcxx/cstdio.h"
#include "stdcxx/cstdlib.h"
#include "stdcxx/cctype.h"
