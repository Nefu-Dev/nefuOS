/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS read syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: read
int sys_read() {
    // Implement read syscall
    return 0;
}

// System call: read with arguments
int sys_read(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement read syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu
