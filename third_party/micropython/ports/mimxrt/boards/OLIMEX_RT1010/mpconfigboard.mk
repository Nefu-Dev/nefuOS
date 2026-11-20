/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = MIMXRT1011
MCU_VARIANT = MIMXRT1011DAE5A

MICROPY_FLOAT_IMPL = single
MICROPY_HW_FLASH_TYPE = qspi_nor_flash
MICROPY_HW_FLASH_SIZE = 0x200000  # 2MB
MICROPY_HW_FLASH_RESERVED ?= 0x1000  # 4KB

CFLAGS += -DMICROPY_HW_FLASH_DQS=kFlexSPIReadSampleClk_LoopbackInternally

SRC_C += \
	hal/flexspi_nor_flash.c \

