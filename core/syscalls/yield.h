/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS yield syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: yield
int sys_yield() {
    // Implement yield syscall
    return 0;
}

// System call: yield with arguments
int sys_yield(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement yield syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu
