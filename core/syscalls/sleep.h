// nefuOS sleep syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: sleep
int sys_sleep() {
    // Implement sleep syscall
    return 0;
}

// System call: sleep with arguments
int sys_sleep(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement sleep syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu