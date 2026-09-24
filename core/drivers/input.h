// nefuOS input driver
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace drivers {

// input driver initialization
bool init_input() {
    // Initialize input hardware
    return true;
}

// input driver shutdown
void shutdown_input() {
    // Shutdown input hardware
}

// input status
bool () {
    return true;
}

// input read operation
int (uint32_t addr, void* buf, uint32_t len) {
    // Read from input
    return (int)len;
}

// input write operation
int (uint32_t addr, const void* buf, uint32_t len) {
    // Write to input
    return (int)len;
}

} // namespace drivers
} // namespace nefu