// nefuOS kill syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: kill
int sys_kill() {
    // Implement kill syscall
    return 0;
}

// System call: kill with arguments
int sys_kill(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement kill syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu