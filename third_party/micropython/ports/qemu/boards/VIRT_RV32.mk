/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
QEMU_ARCH = riscv32
QEMU_MACHINE = virt

CFLAGS += -DQEMU_SOC_VIRT
CFLAGS += -DMICROPY_HW_MCU_NAME='"$(RV32_ARCH)"'

LDSCRIPT = mcu/rv32/virt.ld

SRC_BOARD_O += shared/runtime/gchelper_native.o shared/runtime/gchelper_rv32i.o

MPY_CROSS_FLAGS += -march=rv32imc

# These Thumb tests don't run on RV32, so exclude them.
RUN_TESTS_ARGS = --exclude 'inlineasm|qemu/asm_test'

