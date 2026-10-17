/*
 * mphalport.h — minimal MicroPython HAL for the nefuOS embed (host build).
 *
 * The nefuOS port does not use the interactive REPL, stdin, or time modules;
 * every one of these HAL entry points is defined as a no-op macro so that
 * py/mphal.h will not redeclare them as extern functions (mphal.h guards
 * each with #ifndef mp_hal_xxx).  All interpreter output is routed through
 * MP_PLAT_PRINT_STRN -> nefu_mp_stdout() instead.
 */
#ifndef MICROPY_INCLUDED_MPHALPORT_H
#define MICROPY_INCLUDED_MPHALPORT_H

/* MinGW declares alloca() in <malloc.h> (MicroPython's py/pystack.h uses it). */
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <malloc.h>
#endif

#define mp_hal_stdio_poll(flags) (0)
#define mp_hal_stdin_rx_chr() (-1)
#define mp_hal_stdout_tx_str(str) ((void)(str))
#define mp_hal_stdout_tx_strn(str, len) ((void)(str), (void)(len), 0)
#define mp_hal_stdout_tx_strn_cooked(str, len) ((void)(str), (void)(len))
#define mp_hal_delay_ms(ms) ((void)(ms))
#define mp_hal_delay_us(us) ((void)(us))
#define mp_hal_ticks_ms() (0)
#define mp_hal_ticks_us() (0)
#define mp_hal_ticks_cpu() (0)
#define mp_hal_time_ns() (0)

/* Ctrl-C interrupt handling: not used in the nefuOS embed. */
#define mp_hal_set_interrupt_char(c) ((void)(c))

#endif /* MICROPY_INCLUDED_MPHALPORT_H */
