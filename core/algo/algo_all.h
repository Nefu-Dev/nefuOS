// nefuOS algorithm library — master header (STL-style single include)
//   #include "algo/algo_all.h"
// Exposes nefu::algo (sort/search/graph/ds) and nefu::num (numeric).
// The double-based numeric module is host-only (the bare kernel has no
// soft-double), so it is guarded by NEFU_BARE here.
#pragma once
#include "sort.h"
#include "search.h"
#include "graph.h"
#include "ds.h"
#ifndef NEFU_BARE
#include "numeric.h"
#endif

// combined self test helper: returns total failures across all modules
namespace nefu { namespace algo {
inline int algo_all_self_test() {
    int f = 0;
    f += sort_self_test();
    f += search_self_test();
    f += graph_self_test();
    f += ds_self_test();
#ifndef NEFU_BARE
    f += num::numeric_self_test();
#endif
    return f;
}
} }
