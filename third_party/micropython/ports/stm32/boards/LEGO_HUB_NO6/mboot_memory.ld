/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
/*
    Linker script fragment for mboot on an STM32xxx MCU.
    This defines the memory sections for the bootloader to use.
*/
MEMORY
{
    FLASH_BL (rx)   : ORIGIN = 0x08008000, LENGTH = 32K
    RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 120K
}

/* Location of protected flash area which must not be modified, because mboot lives there. */
_mboot_protected_flash_start = ORIGIN(FLASH_BL);
_mboot_protected_flash_end_exclusive = ORIGIN(FLASH_BL) + LENGTH(FLASH_BL);

