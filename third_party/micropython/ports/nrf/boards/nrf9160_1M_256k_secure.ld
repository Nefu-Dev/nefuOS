/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
/* Specify the memory areas */
MEMORY
{
    FLASH_TEXT (rx) : ORIGIN = 0x00000000,     LENGTH = 32K
    RAM (xrw)       : ORIGIN = 0x20000000,     LENGTH = 64K
}

