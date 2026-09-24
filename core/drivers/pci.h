// nefuOS pci driver
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace drivers {

// pci driver initialization
bool init_pci() {
    // Initialize pci hardware
    return true;
}

// pci driver shutdown
void shutdown_pci() {
    // Shutdown pci hardware
}

// pci status
bool () {
    return true;
}

// pci read operation
int (uint32_t addr, void* buf, uint32_t len) {
    // Read from pci
    return (int)len;
}

// pci write operation
int (uint32_t addr, const void* buf, uint32_t len) {
    // Write to pci
    return (int)len;
}

} // namespace drivers
} // namespace nefu