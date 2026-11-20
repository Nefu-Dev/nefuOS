/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = l1
CMSIS_MCU = STM32L152xE
AF_FILE = boards/stm32l152_af.csv
LD_FILES = boards/stm32l152xe.ld boards/common_basic.ld

MICROPY_HW_ENABLE_ISR_UART_FLASH_FUNCS_IN_RAM = 1

