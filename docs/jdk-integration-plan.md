# JDK Integration Plan for nefuOS

Status: **phase 2 shipped** — clean-room JVM v2 (soft-float, exceptions,
JDK core library, mark-sweep GC, VFS-backed java/io, terminal `java` with
args). OpenJDK port remains the long-term option; the shipped interpreter is
the working deliverable.
Audience: nefuOS maintainers. Language: English (project convention).

## 1. Goal

Give nefuOS the ability to run Java programs. The long-term option is a real
OpenJDK port; the shipped deliverable is a self-contained clean-room JVM core
(`minijvm`) that interprets `.class` files compiled by a desktop `javac`
(JDK 8..21) on a bare-metal kernel with no hardware FPU, no libc math and no
STL.

Constraints inherited from the kernel:

- C++17, English comments, `tools/build_iso.ps1` enumerates `core/**/*.cpp`
  automatically (no per-file build lists).
- Bare-metal build: no hardware FPU, no `double` arithmetic, no libc/STL.
  `klib` provides `String`/`List`/`kalloc`/`kfree`/`ksprintf`.
- All third-party C components are wrapped with `extern "C"` and listed on the
  same "no plain C code" exception list as QuickJS, LVGL and MicroPython.
  minijvm is pure C++17 — no exceptions/RTTI/STL — so it adds nothing to that
  list.

## 2. Shipped: minijvm (clean-room JVM, v2)

Five translation units under `core/apps/`:

| File | Role |
|---|---|
| `minijvm.h` | public API: `minijvm_run_class(class, len, out, ud, argc, argv)`, `MiniJvmVfs` hooks, `minijvm_set_vfs`, `minijvm_stats` |
| `minijvm_math.h` | header-only soft-double math (`mj_*`: sin/cos/tan/atan2/asin/acos/exp/ln/pow/sqrt/cbrt/floor/ceil/round/abs/min/max/rem, IEEE bits helpers, `mj_d_to_bits`/`mj_f_to_bits`) — no hardware FPU, no libm |
| `minijvm_internal.h` | VM structures, value kinds (`int/long/f32/f64/ref/null`), 13 object kinds, shared declarations |
| `minijvm_core.cpp` | class parsing, method lookup, frames/operand stack, mark-sweep GC, strings, boxing, throwables, exception propagation (`propagate_exc`/`find_handler`) |
| `minijvm.cpp` | full bytecode interpreter + entry points |
| `minijvm_builtin.cpp` | JDK library dispatch (String/StringBuilder/Math/wrappers/System/collections/Arrays/Collections/File/streams/PrintStream/Object/throwables) |

### 2.1 Class file reader

- `CAFEBABE` magic check, version tolerance (javac 8..21 verified).
- Full constant-pool tag parsing (all 17 tags incl. MethodHandle/MethodType —
  tolerated, not executed).
- Field / Method / `Code` attribute walking (constant pool, flags,
  descriptors, bytecode, exception table with `any` entries for finally).

### 2.2 Executed instruction set

Full JVM instruction set for typical programs (except `invokedynamic` and
multi-dimensional arrays):

- constants / locals / arrays (all `*aload`/`*astore` incl. long/double
  wide forms, `newarray`, `anewarray`), `ldc/ldc_w/ldc2_w` incl. double
  (bits decoded via `mj_d_from_bits`)
- stack ops, integer/long/float/double arithmetic, `iinc`, comparisons,
  `if_icmp*`/`if_acmp*`/`if*`/`goto`/`tableswitch`/`lookupswitch`
- objects: `new`, `checkcast`, `instanceof`, field access (`getstatic`,
  `putstatic`, `getfield`, `putfield` incl. double via bits), `athrow`,
  `monitorenter/exit` (no-op), `*return`
- calls: `invokevirtual`/`invokespecial`/`invokestatic`/`invokeinterface` —
  same-class methods interpreted; library classes dispatched natively
- class lifecycle: `<clinit>` on first use; `main(String[])` entry with
  `args` from the terminal command line

### 2.3 Exception model

- Throwable family (`java/lang/Throwable`, `Exception`, `RuntimeException`,
  `ArithmeticException`, `ArrayIndexOutOfBoundsException`,
  `NullPointerException`, `IndexOutOfBoundsException`, user classes whose
  name ends in `Exception`/`Error`/`Throwable`): `getMessage`, `getCause`,
  `toString`, `printStackTrace` (to `System.err`), `fillInStackTrace`.
- `try/catch/finally`: the Code exception table is matched at the faulting
  instruction pc (athrow included); `any` entries run finally blocks and
  rethrow via `aload; athrow`.
- Division by zero, array bounds, null dereference and explicit `throw` all
  flow through `propagate_exc` → nearest handler, unwinding frames as needed.

### 2.4 JDK core library (native dispatch)

- `java/lang`: Object (identity), String (length/charAt/equals/substring/
  concat/toString/equalsIgnoreCase), StringBuilder + StringBuffer (append
  all primitive types + char + boolean, toString — javac 9+ compiles `"a"+b`
  to indy unless `-XDstringConcat=inline`, which the test build uses),
  8 boxed wrappers (`Integer/Long/Boolean/Character/Byte/Short/Float/Double`:
  `valueOf`, `intValue/...`, `parseInt/parseLong/parseBoolean/parseDouble`,
  `toString`, `compare`, `MAX_VALUE/MIN_VALUE/POSITIVE_INFINITY/...`),
  `Math` full set (soft-double `mj_*`; `pow`/`atan2` argument order is
  stack-aware: the top of stack is the *second* parameter), `System`
  (out/err/currentTimeMillis), `Class` (getName minimal).
- `java/util`: ArrayList/List (+ abstract parent chain
  AbstractList/AbstractCollection/Collection/Iterable), HashMap/Map
  (+ AbstractMap), HashSet/Set (+ AbstractSet), Iterator (hasNext/next),
  Arrays (sort int/long, copyOf, fill, equals, binarySearch), Collections
  (sort, reverse, min/max for Comparable).
  String equality in collections is content-based (`refs_eq`), matching
  `String.equals` — ldc materializes fresh strings per occurrence.
- `java/io` over the VFS hooks: PrintStream (print/println all types;
  char/boolean special-cased), File (exists/isFile/isDirectory/length/
  getName/getPath/getParent/delete/mkdir/mkdirs/list), FileInputStream /
  FileOutputStream / ByteArrayInputStream / ByteArrayOutputStream /
  InputStream / OutputStream (read/write/close/flush/available).

### 2.5 Memory: mark-sweep GC

- Object table with free list; `MAX_OBJS` arena replaced by mark-sweep.
- Roots: stack-frame locals + operand stacks + static fields + pending
  exception refs; sweep reclaims unreachable objects; `minijvm_stats`
  reports `gc_runs`/`objects_used`/`max_frames`.
- Bounded: `MAX_CP`, `MAX_CODE`, `MAX_FRAMES`, `MAX_STEPS` guards retained.

### 2.6 Verification

Headless suite (real desktop javac, JDK 21):

- `tests/java/{Hello,Strings,Maths,Exceptions,Collections,IO}.java`
  compiled with `javac -XDstringConcat=inline`; `tools/gen_java_embed.py`
  embeds `.class` bytes (skips `$` inner classes) into
  `tests/minijvm_cases.h`; `tests/minijvm_test.cpp` compares interpreter
  output against `.expect` baselines regenerated from the **real JRE**
  (`tools/gen_expect.py`, UTF-8 no BOM, LF).
- `tests/build_minijvm_test.ps1` does javac → embed → build → run in one
  shot. Current status: **6/6 green** (Hello, Strings, Maths incl. full
  float/double + Math set, Exceptions incl. user exception + nested
  try/finally, Collections incl. wrappers/StringBuilder/ArrayList/HashMap/
  HashSet/Arrays/System.arraycopy, IO incl. ByteArray streams + File
  metadata).
- Terminal integration: `java <file.class> [arg ...]` in
  `core/apps/terminal.cpp` (CAFEBABE guard, VFS hooks registered via
  `minijvm_set_vfs` so java/io talks to the real nefuOS filesystem, per-line
  output through `term_print`, error-code to text), plus `man java`.

## 3. Future: OpenJDK port (unchanged option)

The clean-room interpreter is the reference and the shipped JVM; a real
OpenJDK port remains a listed option, following the QuickJS/LVGL/MicroPython
"no plain C code" exception-list pattern with `extern "C"` glue.

1. **JVM core** — port a small JIT-free JVM (embedded OpenJDK subset or a
   maintained clean-room interpreter) into the kernel image; keep the native
   builtin dispatch as the bootstrap class library.
2. **Class library** — grow the builtin set (java/util Random, java/text
   formatting, reflection-lite).
3. **File I/O** — extend the VFS shim (seek, append, permissions).
4. **Heap/GC** — keep mark-sweep; add compaction if fragmentation shows up.
5. **Toolchain** — a `jar`-aware loader so `java app.jar` works, not only a
   single `.class`.
6. **Networking/threads** (stretch) — map `java.lang.Thread` to kernel tasks
   once the kernel gains threads.

### 3.1 Risks

- Bytecode interpreter speed on a 32-bit integer CPU: acceptable for
  educational/demo workloads; no JIT planned.
- Heap pressure: mark-sweep keeps the pure-integer invariant (no pointer
  tagging games).
- OpenJDK's own C++ runtime assumptions (exceptions, RTTI, STL) must be
  avoided — a clean-room core stays preferred over a direct HotSpot subset.

## 4. Acceptance

- Phase 1 (integer-only interpreter) accepted: Hello/Strings green.
- Phase 2 (real-JDK surface) accepted when `tests/minijvm_test.cpp` is
  **6/6 green** against real-JRE baselines and `java <file.class> [args]`
  runs in the terminal with java/io hitting the real NVFS filesystem.
- OpenJDK phase accepted when the ported JVM runs the same suite plus a
  jar-based test without changing the pure-integer rule.
