// nefuOS wait syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: wait
int sys_wait() {
    // Implement wait syscall
    return 0;
}

// System call: wait with arguments
int sys_wait(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement wait syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu