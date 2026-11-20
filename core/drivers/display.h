/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS display / GPU driver layer.
//
// The boot chain (BIOS: Bochs VBE registers in boot.s, UEFI: GRUB GOP
// framebuffer tag in entry.s) already hands the kernel a linear framebuffer
// via the bootinfo block. This driver sits on top of that and:
//
//   1. scans the PCI bus for display adapters (class 0x03) and identifies
//      integrated/discrete GPU models (Intel / AMD / NVIDIA / virtio-gpu /
//      bochs-display / stdvga);
//   2. selects the best framebuffer backend for the detected adapter:
//        - QEMU virtio-gpu (legacy virtio): drives the GPU control queue,
//          allocates a scanout resource and returns its backing memory as
//          the screen buffer (the GUI draws into it, gpu_present() uploads);
//        - QEMU bochs-display: MMIO framebuffer mapped from a PCI BAR;
//        - real hardware (Intel/AMD/NVIDIA iGPU or dGPU): keeps the boot
//          GOP/VBE linear framebuffer. Intel/AMD parts additionally get an
//          iGPU probe that only takes over when the live plane surface is
//          proven to be that same framebuffer; NVIDIA and everything else
//          keep the boot LFB and are reported for lspci / neofetch.
//
// Pure integer, freestanding (no libc/STL/FP). The Win32 host backend does
// not use this driver (it blits a DIB), so the bare-metal-only paths are
// guarded by NEFU_BARE - and the iGPU backend lives entirely inside them: on
// the host build that code, its registers and its backend enum value are not
// compiled at all.
#pragma once

#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace drivers {

// GPU / display adapter classes detected on the PCI bus.
enum GpuKind {
    GPU_NONE = 0,
    GPU_INTEL,      // Intel integrated graphics (i915 family: UHD/Iris/Arc)
    GPU_AMD,        // AMD APU / Radeon integrated graphics
    GPU_NVIDIA,     // NVIDIA discrete / integrated
    GPU_VIRTIO,     // QEMU virtio-gpu
    GPU_BOCHS,      // Bochs VBE / QEMU stdvga (QEMU stdvga exposes VBE regs)
    GPU_VMWARE,     // VMware SVGA II
    GPU_OTHER,      // unrecognized display adapter
};

struct GpuInfo {
    GpuKind   kind;
    uint16_t  vendor;
    uint16_t  device;
    uint8_t   bus;
    uint8_t   dev;
    uint8_t   func;
    uint32_t  bar0;          // raw BAR0 (framebuffer base for bochs-display)
    uint32_t  bar0_mem;      // decoded memory BAR0 address (0 = n/a)
    char      name[56];      // human-readable model string
};

// ---- adapter identity ----
// Returns a vendor string for a PCI vendor id (0x8086 -> "Intel", ...).
const char* gpu_vendor_name(uint16_t vendor);

// Scan the PCI bus for every display adapter (class code 0x03). Fills out[]
// and returns the number found (0 = no display adapter on the bus).
int gpu_scan(GpuInfo* out, int max);

// ---- framebuffer backend ----
// Initialize the display pipeline and return the framebuffer the GUI should
// draw into.
//
//   *fb / *w / *h / *pitch / *bpp  out-params for the screen buffer.
//
// On bare metal the caller first fills these from the bootinfo block (GOP /
// VBE LFB); this function then upgrades them when a dedicated adapter with a
// better backend is found (virtio-gpu / bochs-display). On the Win32 host it
// is a no-op that reports false (the host backend owns its own surface).
bool gpu_setup_framebuffer(uint8_t** fb, int* w, int* h, int* pitch, int* bpp);

// Per-frame present. virtio-gpu: TRANSFER_TO_HOST_2D + RESOURCE_FLUSH so the
// emulated display shows the new pixels. Real hardware / bochs-display: the
// framebuffer is directly visible, nothing to do.
void gpu_present();

// True when a dedicated adapter (virtio-gpu / bochs-display) owns the screen.
bool gpu_owns_screen();

// Log the detected adapter list (debug output, called from kernel main).
void gpu_log_adapters();

} // namespace drivers
} // namespace nefu
