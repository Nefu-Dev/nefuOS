// nefuOS network driver - real NIC probe framework (header-only).
// Supports the mainstream families: Intel PRO/1000 (e1000), Realtek RTL8139
// and virtio-net; the actual register-level drivers live in core/net/net.cpp.
#pragma once

#include "pci.h"

namespace nefu {
namespace drivers {

enum NicType {
    NIC_NONE = 0,
    NIC_E1000,
    NIC_RTL8139,
    NIC_VIRTIO
};

// Name of a NIC type (for logs / UI).
static inline const char* nic_type_name(NicType t) {
    switch (t) {
        case NIC_E1000:  return "Intel PRO/1000 (e1000)";
        case NIC_RTL8139: return "Realtek RTL8139";
        case NIC_VIRTIO: return "Virtio Network Device";
        default:         return "No NIC";
    }
}

// Probe the whole PCI bus for the first supported NIC.
// Returns the type; *vid/*did (optional) receive the matching vendor/device.
static inline NicType nic_probe(uint16_t* vid, uint16_t* did) {
    PciDev list[64];
    int n = pci_scan(list, 64);
    for (int i = 0; i < n; i++) {
        if (list[i].class_code != 0x02) continue;          // network controller
        if (pci_is_e1000(list[i].vendor, list[i].device)) {
            if (vid) *vid = list[i].vendor;
            if (did) *did = list[i].device;
            return NIC_E1000;
        }
        if (list[i].vendor == 0x10EC && list[i].device == 0x8139) {
            if (vid) *vid = list[i].vendor;
            if (did) *did = list[i].device;
            return NIC_RTL8139;
        }
        if (list[i].vendor == 0x1AF4) {                     // Red Hat virtio
            if (vid) *vid = list[i].vendor;
            if (did) *did = list[i].device;
            return NIC_VIRTIO;
        }
    }
    if (vid) *vid = 0;
    if (did) *did = 0;
    return NIC_NONE;
}

// True if any supported NIC was found on the bus.
static inline bool nic_present(void) {
    return nic_probe(0, 0) != NIC_NONE;
}

} // namespace drivers
} // namespace nefu
