/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
CMSIS_MCU = RA6M1
MCU_SERIES = m4
LD_FILES = boards/EK_RA6M1/ra6m1_ek.ld

# MicroPython settings
MICROPY_VFS_FAT = 1

CFLAGS+=-DDEFAULT_DBG_CH=0

