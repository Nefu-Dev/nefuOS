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