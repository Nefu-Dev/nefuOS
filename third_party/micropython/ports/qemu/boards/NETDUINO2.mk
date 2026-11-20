/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
QEMU_ARCH = arm
QEMU_MACHINE = netduino2

CFLAGS += -mthumb -mcpu=cortex-m3 -mfloat-abi=soft
CFLAGS += -DQEMU_SOC_STM32
CFLAGS += -DMICROPY_HW_MCU_NAME='"STM32"'

LDSCRIPT = mcu/arm/stm32.ld

SRC_BOARD_O = shared/runtime/gchelper_native.o shared/runtime/gchelper_thumb2.o

MPY_CROSS_FLAGS += -march=armv7m

