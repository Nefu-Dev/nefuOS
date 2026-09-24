// nefuOS write syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: write
int sys_write() {
    // Implement write syscall
    return 0;
}

// System call: write with arguments
int sys_write(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement write syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu