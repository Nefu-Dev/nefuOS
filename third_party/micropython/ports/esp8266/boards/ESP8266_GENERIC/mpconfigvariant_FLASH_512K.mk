/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
LD_FILES = boards/esp8266_512kiB.ld

# Note: Use the minimal manifest.py.
FROZEN_MANIFEST ?= $(BOARD_DIR)/manifest_512kiB.py

# Configure mpconfigboard.h.
CFLAGS += -DMICROPY_ESP8266_512K

