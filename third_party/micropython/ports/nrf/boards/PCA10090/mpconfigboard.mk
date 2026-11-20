/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = m33
MCU_VARIANT = nrf91
MCU_SUB_VARIANT = nrf9160
LD_FILES += boards/nrf9160_1M_256k.ld

NRF_DEFINES += -DNRF9160_XXAA -DNRF_TRUSTZONE_NONSECURE

MICROPY_VFS_LFS2 = 1

