/*
 * mpconfigport.h — MicroPython configuration for the nefuOS embed.
 *
 * This port is compiled as plain C with gcc on the nefuOS *host* build
 * (the win32 backend), where the standard C runtime is available.
 * All interpreter output is redirected to the nefuOS terminal through
 * nefu_mp_stdout() (see MP_PLAT_PRINT_STRN below).
 */
#include <stdint.h>
#include <stddef.h>

/* MinGW declares alloca() in <malloc.h> (MicroPython's py/pystack.h uses it). */
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <malloc.h>
#endif

/* ROM feature level: EXTRA_FEATURES gives a well rounded interpreter
   (floats, math, io, memoryview, ...) while staying inside the py/ core. */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

/* Disable ROM text compression: keeps header generation simple and
   avoids the compressed.data.h step (MP_ERROR_TEXT stays verbatim). */
#define MICROPY_ROM_TEXT_COMPRESSION (0)

/* Enable the built-in Python compiler. */
#define MICROPY_ENABLE_COMPILER (1)

/* Garbage collector + static heap (sized below). */
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HEAP_SIZE (131072)   /* 128 KiB static heap */

/* Use setjmp/longjmp based exceptions: portable across host and bare x86. */
#define MICROPY_NLR_SETJMP (1)
/* Capture GC roots with setjmp too (no architecture specific asm helper). */
#define MICROPY_GCREGS_SETJMP (1)

/* We run source strings / files read through the nefuOS VFS; external import
   is enabled and wired to the nefuOS filesystem via nefu_mp_set_vfs() so a
   script can `import` its own .py modules. No frozen modules, no readline
   based interactive REPL helper. */
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_MODULE_FROZEN_MPY (0)
#define MICROPY_MODULE_FROZEN_STR (1) /* asyncio package frozen as strings */
#define MICROPY_HELPER_REPL (0)

/* select module (needed by asyncio); polls via stream ioctl (no-op here). */
#define MICROPY_PY_SELECT (1)

/* Float implementation: use host double for numeric fidelity. */
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_FLOAT (1)
#define MICROPY_PY_BUILTINS_COMPLEX (1)
/* Do not rely on GCC's native _Float16: py/binary.c would then call the
   libgcc helpers __extendhfdf2/__truncsfhf2, which a freestanding kernel
   (no libgcc) cannot resolve.  Using the portable bit-manipulation path for
   the struct 'e' half-float format keeps the kernel self-contained. */
#define MICROPY_FLOAT_USE_NATIVE_FLT16 (0)

/* Provide a sys module. */
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PLATFORM "nefuos"
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (1) /* sys.path = ["", ".frozen"] */
#define MICROPY_PY_SYS_ARGV (1)

/* No threading HAL in the nefuOS embed: disable the _thread module. */
#define MICROPY_PY_THREAD (0)

/* No interactive stdin in the embed: the input() builtin stays off (it needs
   shared/readline + a live console).  The io module / open() builtin are on
   and wired to the nefuOS VFS via nefu_mp_set_vfs() (memory-backed files). */
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_PY_IO (1)

/* ---- standard-library modules (extmod) ---------------------------------
   Compiled into the build so the interpreter supports the full instruction
   set.  These are pure-Python-level modules with no hardware, network or
   filesystem dependency. */
#define MICROPY_PY_BINASCII     (1)
#define MICROPY_PY_HEAPQ        (1)
#define MICROPY_PY_JSON         (1)
#define MICROPY_PY_RANDOM       (1)
#define MICROPY_PY_UCTYPES      (1)
#define MICROPY_PY_ASYNCIO      (1)
#define MICROPY_PY_FRAMEBUF     (1)
#define MICROPY_PY_PLATFORM     (1)
#define MICROPY_PY_RE           (1)
#define MICROPY_PY_TIME         (1)
#define MICROPY_PY_HASHLIB      (1)
#define MICROPY_PY_DEFLATE      (1)

#define MICROPY_HW_BOARD_NAME "nefuOS"
#define MICROPY_HW_MCU_NAME "x86-64"

/* Redirect every interpreter print to the nefuOS terminal callback. */
void nefu_mp_stdout(const char *str, size_t len);
#define MP_PLAT_PRINT_STRN(str, len) nefu_mp_stdout((str), (len))

/* mp_int_t / mp_uint_t must be pointer sized (host is 64-bit). */
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;

#define MP_STATE_PORT MP_STATE_VM
