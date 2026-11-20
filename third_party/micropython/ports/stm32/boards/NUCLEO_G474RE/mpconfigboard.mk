/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
# MCU settings
MCU_SERIES = g4
CMSIS_MCU = STM32G474xx
MICROPY_FLOAT_IMPL = single
AF_FILE = boards/stm32g474_af.csv
LD_FILES = boards/stm32g474.ld boards/common_basic.ld

MICROPY_HW_ENABLE_ISR_UART_FLASH_FUNCS_IN_RAM = 1

