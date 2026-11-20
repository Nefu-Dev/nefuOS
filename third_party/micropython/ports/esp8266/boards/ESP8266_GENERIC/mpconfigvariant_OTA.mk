/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
LD_FILES = boards/esp8266_ota.ld

MICROPY_PY_ESPNOW ?= 1
MICROPY_PY_BTREE ?= 1
MICROPY_VFS_LFS2 ?= 1

# Note: Implicitly uses the port manifest.

# Configure mpconfigboard.h.
CFLAGS += -DMICROPY_ESP8266_1M

