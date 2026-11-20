/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
/* This linker script fragment is intended to be included in SECTIONS. */

/* The startup code goes first into FLASH */
.isr_vector :
{
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
} >FLASH_ISR

