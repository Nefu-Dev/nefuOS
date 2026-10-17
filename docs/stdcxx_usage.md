# nefuCpp C++ Standard Library (core/stdcxx) Usage Guide

## Overview

`core/stdcxx/` is a **self-contained, header-only** C++ standard-library subset

for nefuCpp (the nefuOS C++ application packager). Containers and helpers are

implemented on the kernel primitives and live in the isolated namespace

`nefu::stdx`, so they never collide with the host `std`.

> Dependency: kernel-heap allocation (default 
>
> `operator new/delete`
>
> ; on a real
> kernel target define 
>
> `NEFU_STDX_USE_NEUF_KALLOC`
>
>  to switch to
> `kalloc/kfree`
>
> ).



***

## Including the Library



```
\#include "stdcxx/stdcxx\_all.h"   // aggregate header: pulls in every component

using namespace nefu::stdx;      // or qualify individually (nefu::stdx::vector, ...)
```

> When packaging with 
>
> `nefucpp`
>
> , 
>
> `stdcxx/stdcxx_all.h`
>
>  is in the
> "Automatically includes" list (the 
>
> `C++ standard library`
>
>  line), so it is
> pulled in automatically.

You can also include only what you need:



```
\#include "stdcxx/vector.h"

\#include "stdcxx/string.h"
```



***

## What Is Provided



| Area           | Headers                                                               |
| -------------- | --------------------------------------------------------------------- |
| Core           | `_core.h` (allocator hook, move/forward, size\_t/nullptr\_t)          |
| Types / traits | `cstddef` `cstdint` `type_traits` `limits` `utility`                  |
| Smart pointer  | `memory` (`unique_ptr` / `make_unique`)                               |
| Iterators      | `iterator` (tags, begin/end, find)                                    |
| Containers     | `array` `vector` `list` `string` `map` `set` `queue` `stack`          |
| Algorithms     | `algorithm` (sort / count / reverse / min / max / clamp / accumulate) |
| Numeric        | `numeric` (accumulate / iota)                                         |
| C functions    | `cstring` `cmath` `cstdio` `cstdlib` `cctype`                         |

`map` and `set` are backed by an internal AVL tree (`_avltree.h`), giving

balanced, ordered iteration.



***

## Containers

### vector — dynamic array



```
vector\<int> v;

v.push\_back(1); v.push\_back(2); v.push\_back(3);

int x = v\[0];            // 1

size\_t n = v.size();     // 3

v.pop\_back();            // remove the last element

v.clear();               // clear
```

### array — fixed-size array



```
array\<int, 4> a;

a.fill(7);

int x = a\[3];            // 7

size\_t n = a.size();     // 4
```

### string — dynamic string



```
string s = "hello";

s += " world";

size\_t n = s.size();     // 11

size\_t pos = s.find('w');// 6

string t = s.substr(0, 5);// "hello"

const char\* c = s.c\_str();

bool eq = (s == "hello world");
```

### list — doubly-linked list



```
list\<int> l;

l.push\_back(1); l.push\_front(0);

int f = l.front();       // 0

int b = l.back();        // 1

l.pop\_back(); l.pop\_front();
```

### map / set — ordered containers (AVL tree)



```
map\<string, int> m;

m\["apple"] = 3;             // insert / overwrite

if (m.contains("apple")) {

&#x20;   int v = m\["apple"];     // 3

}

m.erase("apple");           // remove

set\<int> st;

st.insert(5); st.insert(5); // duplicate insert is a no-op

bool has = st.contains(5);  // true

size\_t cnt = st.size();     // 1
```

Iterating (ordered):



```
for (map\<string,int>::iterator it = m.begin(); it != m.end(); ++it) {

&#x20;   // it->first (key), it->second (value)

}
```

### queue / stack — adaptors (over list)



```
queue\<int> q;

q.push(1); q.push(2);

int f = q.front();          // 1

q.pop();

stack\<int> sk;

sk.push(1); sk.push(2);

int top = sk.top();         // 2

sk.pop();
```



***

## Algorithms



```
vector\<int> v; v.push\_back(3); v.push\_back(1); v.push\_back(2);

sort(v.begin(), v.end());            // heap sort, O(n log n), in place

int acc = accumulate(v.begin(), v.end(), 0);   // sum = 6

int mx = max(1, 9);                  // 9

int mn = min(1, 9);                  // 1

int cl = clamp(15, 0, 10);           // 10

size\_t c = count(v.begin(), v.end(), 2);       // 1

reverse(v.begin(), v.end());         // reverse in place
```



***

## pair / Smart Pointer / Traits



```
pair\<int, string> p = make\_pair(1, "one");

p.first; p.second;                   // 1 / "one"

unique\_ptr\<int> up(new int(42));

if (up) { int v = \*up; }             // 42; freed automatically on destruction

up.reset(new int(7));                // take ownership of a new object

// numeric\_limits / type\_traits

int mx = numeric\_limits\<int>::max(); // 2147483647

static\_assert(is\_integral\<int>::value, "int is integral");
```



***

## C Function Wrappers

`cstring` / `cmath` / `cstdio` / `cstdlib` / `cctype` bring the host C functions

into `nefu::stdx`, so they can be called directly in nefuCpp programs:



```
\#include "stdcxx/stdcxx\_all.h"

using namespace nefu::stdx;

int main() {

&#x20;   const char\* s = "nefuOS";

&#x20;   size\_t n = strlen(s);            // 6

&#x20;   char buf\[16]; snprintf(buf, sizeof(buf), "%s %d", s, 2026);

&#x20;   double r = sqrt(2.0);

&#x20;   return 0;

}
```



***

## Building & Self-Test

The library is header-only and is not part of the nefuOS kernel build;

correctness is guaranteed by an independent self-test:



```
// core/stdcxx/test\_stdcxx.cpp — asserts for every container/algorithm/trait
```



```
g++ -std=c++11 -I core -o test\_stdcxx.exe core/stdcxx/test\_stdcxx.cpp

./test\_stdcxx.exe      # -> stdcxx self-test: PASS  (failures=0)
```



***

## Notes



1. **Namespace** — everything lives in `nefu::stdx`, isolated from the real

   `std`, so both can coexist safely.

2. **Allocator** — host heap by default; define `NEFU_STDX_USE_NEUF_KALLOC` on a

   kernel target to use `kalloc/kfree` (see `_core.h`).

3. **Scope** — this is a teaching/embedded subset, not the full standard library

   (no `unordered_map`, no exceptions, no `<thread>`).