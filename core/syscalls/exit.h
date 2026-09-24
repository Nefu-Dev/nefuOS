// nefuOS exit syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: exit
int sys_exit() {
    // Implement exit syscall
    return 0;
}

// System call: exit with arguments
int sys_exit(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement exit syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu