/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
/*
    GNU linker script for NRF52832
*/

_flash_size = 512K;
_ram_size   = 64K;

/* produce a link error if there is not this amount of RAM for these sections */
_stack_size = 8K;
_minimum_heap_size = 32K;

