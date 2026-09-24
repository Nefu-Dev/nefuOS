// nefuOS exec syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: exec
int sys_exec() {
    // Implement exec syscall
    return 0;
}

// System call: exec with arguments
int sys_exec(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement exec syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu