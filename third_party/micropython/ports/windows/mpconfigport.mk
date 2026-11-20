/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
# Enable/disable modules and 3rd-party libs to be included in interpreter

# Build 32-bit binaries on a 64-bit host
MICROPY_FORCE_32BIT ?= 0

# This variable can take the following values:
#  0 - no readline, just simple stdin input
#  1 - use MicroPython version of readline
MICROPY_USE_READLINE ?= 1

