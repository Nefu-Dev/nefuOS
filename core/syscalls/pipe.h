// nefuOS pipe syscall
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace syscalls {

// System call: pipe
int sys_pipe() {
    // Implement pipe syscall
    return 0;
}

// System call: pipe with arguments
int sys_pipe(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // Implement pipe syscall with arguments
    return 0;
}

} // namespace syscalls
} // namespace nefu