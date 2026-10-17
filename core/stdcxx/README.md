# nefuCpp C++ Standard Library (core/stdcxx)

A self-contained, header-only C++ standard-library subset for **nefuCpp**
(the nefuOS C++ application packager). Every container and helper is
implemented on the nefuOS kernel primitives and lives in its own namespace so
it never collides with the real `std`.

## Using it

In a nefuCpp program:

```cpp
#include "stdcxx/stdcxx_all.h"
using namespace nefu::stdx;

int main() {
    vector<int> v; v.push_back(1); v.push_back(2); v.push_back(3);
    map<string, int> m; m["a"] = 1;
    return 0;
}
```

`nefucpp` lists `stdcxx/stdcxx_all.h` in its "Automatically includes" block,
so packaged applications pick the library up by default.

## What is provided

| Area            | Headers                                                        |
|-----------------|----------------------------------------------------------------|
| Core            | `_core.h` (allocator hook, move/forward, size_t/nullptr_t)      |
| Types / traits  | `cstddef` `cstdint` `type_traits` `limits` `utility`           |
| Smart pointer   | `memory` (unique_ptr / make_unique)                            |
| Iterators       | `iterator` (tags, begin/end, find)                             |
| Containers      | `array` `vector` `list` `string` `map` `set` `queue` `stack`   |
| Algorithms      | `algorithm` (sort/count/reverse/min/max/clamp/accumulate)      |
| Numeric         | `numeric`                                                      |
| C functions     | `cstring` `cmath` `cstdio` `cstdlib` `cctype`                  |
| Map/Set backing | `_avltree` (internal AVL tree, key+value nodes)                |

`map` and `set` are backed by an internal AVL tree (`_avltree.h`) that keeps
balanced, ordered iteration.

## Allocator hook

By default (host/verification builds) the library uses `::operator new/delete`.
In a real kernel target, define `NEFU_STDX_USE_NEUF_KALLOC` so the backing
allocator becomes `kalloc/kfree` from `core/klib/klib.h` (see `_core.h`).

## Verification

`test_stdcxx.cpp` exercises every container and helper and is the build gate:

```
g++ -std=c++11 -I core -o test_stdcxx.exe core/stdcxx/test_stdcxx.cpp
./test_stdcxx.exe   # -> stdcxx self-test: PASS
```
