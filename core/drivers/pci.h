// nefuOS PCI driver - full real PCI enumeration + config access (header-only).
// Pure integer, freestanding.  Used by the NIC / audio / storage drivers and
// by the legacy nefu::hw bridge below.
#pragma once

#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace drivers {

// ===================== port I/O =====================
static inline uint8_t pci_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t pci_inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint32_t pci_inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void pci_outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void pci_outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void pci_outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline void pci_io_wait() { pci_outl(0x80, 0); }

// ===================== PCI configuration space =====================
#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

static inline uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    pci_outl(PCI_CONFIG_ADDRESS, addr);
    return pci_inl(PCI_CONFIG_DATA);
}

static inline void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t value) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    pci_outl(PCI_CONFIG_ADDRESS, addr);
    pci_outl(PCI_CONFIG_DATA, value);
}

static inline uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t v = pci_config_read32(bus, dev, func, reg);
    return (uint16_t)((reg & 2) ? (v >> 16) : (v & 0xFFFF));
}

static inline void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t value) {
    uint32_t cur = pci_config_read32(bus, dev, func, reg);
    uint32_t mask = (reg & 2) ? 0xFFFF0000u : 0x0000FFFFu;
    uint32_t v = (cur & ~mask) | (((uint32_t)value << ((reg & 2) ? 16 : 0)) & mask);
    pci_config_write32(bus, dev, func, reg, v);
}

static inline uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t v = pci_config_read32(bus, dev, func, reg);
    uint32_t shift = (uint32_t)(reg & 3) * 8u;
    return (uint8_t)((v >> shift) & 0xFF);
}

// ===================== device record =====================
struct PciDev {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint32_t bars[6];      // raw BAR values (BAR0..BAR5)
    uint8_t  irq_line;
    bool     valid;
};

static inline PciDev pci_read_device(uint8_t bus, uint8_t dev, uint8_t func) {
    PciDev d;
    d.bus = bus; d.dev = dev; d.func = func; d.valid = false;
    uint32_t id = pci_config_read32(bus, dev, func, 0x00);
    d.vendor = (uint16_t)(id & 0xFFFF);
    d.device = (uint16_t)((id >> 16) & 0xFFFF);
    if (d.vendor == 0xFFFF) return d;          // nothing there
    if (d.vendor == 0x0000 && d.device == 0x0000) return d;
    uint32_t cls = pci_config_read32(bus, dev, func, 0x08);
    d.class_code = (uint8_t)((cls >> 24) & 0xFF);
    d.subclass  = (uint8_t)((cls >> 16) & 0xFF);
    d.prog_if   = (uint8_t)((cls >> 8) & 0xFF);
    for (int i = 0; i < 6; i++) d.bars[i] = pci_config_read32(bus, dev, func, (uint8_t)(0x10 + i * 4));
    d.irq_line = pci_config_read8(bus, dev, func, 0x3C);
    d.valid = true;
    return d;
}

// Full-bus scan (walks PCI-PCI bridges). Returns the number of devices found.
static inline int pci_scan(PciDev* out, int max) {
    if (!out || max <= 0) return 0;
    int count = 0;
    uint8_t stack[16];
    int sp = 0;
    stack[sp++] = 0;                       // start at bus 0
    while (sp > 0 && count < max) {
        uint8_t bus = stack[--sp];
        for (uint8_t dev = 0; dev < 32 && count < max; dev++) {
            uint8_t hdr = pci_config_read8(bus, dev, 0, 0x0E);
            uint8_t nfunc = (hdr & 0x80) ? 8u : 1u;
            for (uint8_t func = 0; func < nfunc && count < max; func++) {
                PciDev d = pci_read_device(bus, dev, func);
                if (!d.valid) continue;
                out[count++] = d;
                // PCI-PCI bridge (class 0x06 subclass 0x04): push secondary bus
                if (d.class_code == 0x06 && d.subclass == 0x04) {
                    uint8_t sec = pci_config_read8(bus, dev, func, 0x19);
                    if (sp < 16 && sec != 0 && sec != bus) stack[sp++] = sec;
                }
            }
        }
    }
    return count;
}

// Find the first device with a given vendor/device pair.
static inline bool pci_find(uint16_t vendor, uint16_t device, PciDev& out) {
    PciDev list[64];
    int n = pci_scan(list, 64);
    for (int i = 0; i < n; i++) {
        if (list[i].vendor == vendor && list[i].device == device) {
            out = list[i];
            return true;
        }
    }
    out.valid = false;
    return false;
}

// Find the first device of a PCI class (e.g. class 0x02 network).
static inline bool pci_find_class(uint8_t class_code, PciDev& out) {
    PciDev list[64];
    int n = pci_scan(list, 64);
    for (int i = 0; i < n; i++) {
        if (list[i].class_code == class_code) {
            out = list[i];
            return true;
        }
    }
    out.valid = false;
    return false;
}

// Enable bus mastering on a device (needed for DMA NICs / sound cards).
static inline void pci_enable_busmaster(const PciDev& d) {
    uint16_t cmd = pci_config_read16(d.bus, d.dev, d.func, 0x04);
    cmd |= 0x0006u;                       // memory space + IO space
    cmd |= 0x0004u;                       // bus master
    pci_config_write16(d.bus, d.dev, d.func, 0x04, cmd);
}

// Decode a BAR into a usable address.  *is_io is set for IO-space bars.
// 64-bit bars are combined from BAR[i] | BAR[i+1]<<32.
static inline uint64_t pci_bar_address(const PciDev& d, int idx, bool* is_io) {
    if (idx < 0 || idx > 5) { if (is_io) *is_io = false; return 0; }
    uint32_t raw = d.bars[idx];
    if (raw == 0) { if (is_io) *is_io = false; return 0; }
    if (raw & 1u) {                           // IO space
        if (is_io) *is_io = true;
        return (uint64_t)(raw & 0xFFFFFFFCu);
    }
    if (is_io) *is_io = false;                // memory space
    uint64_t base = (uint64_t)(raw & 0xFFFFFFF0u);
    if ((raw & 0x00000006u) == 0x00000006u && idx < 5) {   // 64-bit bar
        uint32_t hi = d.bars[idx + 1];
        base |= ((uint64_t)hi << 32);
    }
    return base;
}

// ===================== vendor / device naming =====================
static inline const char* pci_vendor_name(uint16_t vid) {
    switch (vid) {
        case 0x8086: return "Intel";
        case 0x10EC: return "Realtek";
        case 0x1AF4: return "Red Hat / Virtio";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1106: return "VIA";
        case 0x14E4: return "Broadcom";
        case 0x11AB: return "Marvell";
        case 0x1969: return "Atheros";
        default:     return "Unknown";
    }
}

// Intel PRO/1000 (e1000) family check: covers 82540EM..82576 and the
// newer I210/I211/I217/82579 set used by mainstream boards.
static inline bool pci_is_e1000(uint16_t vid, uint16_t did) {
    if (vid != 0x8086) return false;
    if (did >= 0x1000 && did <= 0x10FF) return true;   // classic 8254x/8257x
    if (did >= 0x1500 && did <= 0x15FF) return true;   // I210/I211/I217/82579
    switch (did) {
        default:
            return false;
    }
}


} // namespace drivers
} // namespace nefu

// ===================== legacy bridge (core/hw/detect.h) =====================
// Older core code declares nefu::hw::pci_read_config / pci_write_config.
namespace nefu {
namespace hw {

static inline uint32_t pci_read_config(uint32_t bus, uint32_t dev, uint32_t func, uint32_t reg) {
    return drivers::pci_config_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)func, (uint8_t)reg);
}
static inline void pci_write_config(uint32_t bus, uint32_t dev, uint32_t func, uint32_t reg, uint32_t val) {
    drivers::pci_config_write32((uint8_t)bus, (uint8_t)dev, (uint8_t)func, (uint8_t)reg, val);
}

} // namespace hw
} // namespace nefu
