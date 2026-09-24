// nefuOS signal syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: signal
int sys_signal() {
    // Implement signal syscall
    return 0;
}

// System call: signal with arguments
int sys_signal(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement signal syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu