// nefuOS fork syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: fork
int sys_fork() {
    // Implement fork syscall
    return 0;
}

// System call: fork with arguments
int sys_fork(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement fork syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu