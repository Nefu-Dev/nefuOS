/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
/* GNU linker script for Adafruit nrf52840 Bootloader */

_bootloader_head_size = 0x1000;  /* MBR */
_bootloader_tail_size = 0xC000;  /* Bootloader start address 0x000F4000 */

