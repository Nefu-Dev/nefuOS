/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv3 <https://www.gnu.org/licenses/gpl-3.0.html>
 * See COPYING for full license text.
 */
BOARD_PCA10059_DIR = boards/PCA10059/modules

INC += -I./$(BOARD_PCA10059_DIR)
CFLAGS += -DBOARD_SPECIFIC_MODULES

SRC_BOARD_MODULES = $(addprefix $(BOARD_PCA10059_DIR)/,\
                      recover_uicr_regout0.c \
                      )

OBJ += $(addprefix $(BUILD)/, $(SRC_BOARD_MODULES:.c=.o))


