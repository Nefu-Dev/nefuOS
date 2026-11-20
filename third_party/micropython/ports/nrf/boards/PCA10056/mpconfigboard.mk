/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = m4
MCU_VARIANT = nrf52
MCU_SUB_VARIANT = nrf52840
SOFTDEV_VERSION = 6.1.1
LD_FILES += boards/nrf52840_1M_256k.ld

NRF_DEFINES += -DNRF52840_XXAA

MICROPY_VFS_LFS2 = 1

