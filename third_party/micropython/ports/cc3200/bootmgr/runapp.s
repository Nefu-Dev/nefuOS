/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
    .syntax unified
    .cpu cortex-m4
    .thumb
    .text
    .align  2

@ void bootmgr_run_app(_u32 base)
    .global bootmgr_run_app
    .thumb
    .thumb_func
    .type   bootmgr_run_app, %function
bootmgr_run_app:
    @ set the SP
    ldr    sp, [r0]
    add    r0, r0, #4

    @ jump to the entry code
    ldr    r1, [r0]
    bx     r1

