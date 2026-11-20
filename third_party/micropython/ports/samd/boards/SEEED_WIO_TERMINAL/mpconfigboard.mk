/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
MCU_SERIES = SAMD51
CMSIS_MCU = SAMD51P19A
LD_FILES = boards/samd51x19a.ld sections.ld
TEXT0 = 0x4000

# The ?='s allow overriding in mpconfigboard.mk.
# MicroPython settings
MICROPY_HW_CODESIZE ?= 496K

