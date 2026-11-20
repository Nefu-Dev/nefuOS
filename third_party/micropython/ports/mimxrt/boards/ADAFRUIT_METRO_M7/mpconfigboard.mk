/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = MIMXRT1011
MCU_VARIANT = MIMXRT1011DAE5A

MICROPY_FLOAT_IMPL = single
MICROPY_HW_FLASH_TYPE ?= qspi_nor_flash
MICROPY_HW_FLASH_SIZE ?= 0x800000  # 8MB

MICROPY_PY_NETWORK_NINAW10 ?= 1
MICROPY_PY_SSL ?= 1
MICROPY_SSL_MBEDTLS ?= 1

