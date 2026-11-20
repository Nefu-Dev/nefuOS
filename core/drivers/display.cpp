/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS display / GPU driver implementation.
// See display.h for the design overview.
//
// Bare-metal backends implemented here:
//   1. QEMU virtio-gpu  (legacy virtio, PCI 1AF4:1050, class 0x03):
//      full control-queue driver - GET_DISPLAY_INFO, RESOURCE_CREATE_2D,
//      RESOURCE_ATTACH_BACKING, SET_SCANOUT, and per-frame
//      TRANSFER_TO_HOST_2D + RESOURCE_FLUSH in gpu_present().
//   2. QEMU bochs-display (PCI 1234:1111): framebuffer mapped straight from
//      BAR0 (no command queue - the pixels are directly visible).
//   3. Real hardware (Intel/AMD/NVIDIA): the boot GOP/VBE LFB from the
//      bootinfo block stays in charge; the adapter is identified and logged.
//
// Pure integer, freestanding. Win32 host backend compiles this file too but
// every hardware path is inside #ifdef NEFU_BARE and returns false there.
#include "display.h"
#include "pci.h"

#ifdef NEFU_BARE
#include <stdint.h>

// ---- port I/O helpers (bare) ----
static inline uint8_t  gp_inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline uint16_t gp_inw(uint16_t port) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline uint32_t gp_inl(uint16_t port) { uint32_t v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static inline void gp_outb(uint16_t port, uint8_t v)  { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(port)); }
static inline void gp_outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(port)); }
static inline void gp_outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0,%1" : : "a"(v), "Nd"(port)); }
static inline void gp_iowait() { gp_outl(0x80, 0); }
#endif

namespace nefu {
namespace drivers {

// ===================== adapter identity =====================

const char* gpu_vendor_name(uint16_t vendor) {
    switch (vendor) {
        case 0x8086: return "Intel";
        case 0x1002: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1AF4: return "Red Hat/QEMU";
        case 0x1234: return "QEMU";
        case 0x15AD: return "VMware";
        case 0x1414: return "Microsoft";
        default:     return "Unknown";
    }
}

// Model lookup tables (vendor,device) -> human readable name. Covers the
// common integrated (iGPU) families plus the virtual adapters we drive.
struct GpuModel { uint16_t dev; const char* name; };

static const char* intel_model(uint16_t dev) {
    switch (dev) {
        case 0x0042: case 0x0046: return "Intel HD Graphics (Clarkdale/Arrandale)";
        case 0x0106: case 0x0116: case 0x0126: return "Intel HD Graphics 2000/3000 (Sandy Bridge)";
        case 0x0156: case 0x0166: case 0x016a: return "Intel HD Graphics 2500/4000 (Ivy Bridge)";
        case 0x0412: case 0x041e: case 0x0d22: return "Intel HD Graphics 4600 (Haswell)";
        case 0x0a16: case 0x0a2e: return "Intel HD Graphics (Bay Trail)";
        case 0x1912: case 0x1916: case 0x1926: return "Intel HD Graphics 520/530 (Skylake)";
        case 0x5912: case 0x5916: case 0x591b: return "Intel HD/UHD Graphics (Kaby Lake)";
        case 0x3e92: case 0x3e93: case 0x3ea0: return "Intel UHD Graphics 630 (Coffee Lake)";
        case 0x9bc4: case 0x9bc8: case 0x9bca: return "Intel Iris Plus Graphics (Ice Lake)";
        case 0x8a52: case 0x8a53: case 0x8a5a: return "Intel Iris Xe Graphics (Tiger Lake)";
        case 0x4680: case 0x4682: case 0x4692: return "Intel Iris Xe Graphics (Alder Lake)";
        case 0x7d55: case 0xa780: case 0xa781: return "Intel UHD Graphics 700 series (Raptor Lake/Alder Lake)";
        default: return "Intel integrated graphics";
    }
}

static const char* amd_model(uint16_t dev) {
    switch (dev) {
        case 0x15dd: case 0x15e7: return "AMD Radeon Vega 8/11 (Raven Ridge/Picasso)";
        case 0x1636: case 0x1638: return "AMD Radeon Vega 6/8 (Renoir)";
        case 0x164c: return "AMD Radeon Graphics (Van Gogh)";
        case 0x15e8: case 0x164e: case 0x1681: return "AMD Radeon Graphics (Cezanne)";
        case 0x73ef: case 0x73ff: return "AMD Radeon RX 7000 (Navi 31)";
        case 0x744c: case 0x744d: return "AMD Radeon RX 9000 (Navi 4x)";
        default: return "AMD Radeon integrated graphics";
    }
}

static const char* nvidia_model(uint16_t dev) {
    switch (dev) {
        case 0x1a81: return "NVIDIA GeForce GTX 10 series (Pascal)";
        case 0x1b81: case 0x1b82: return "NVIDIA GeForce GTX 1080/1070 (Pascal)";
        case 0x2206: return "NVIDIA GeForce GTX 1660 (Turing)";
        case 0x2487: return "NVIDIA GeForce RTX 3060 (Ampere)";
        case 0x2684: return "NVIDIA GeForce RTX 4090 (Ada)";
        case 0x2c00: return "NVIDIA GeForce RTX 5070 (Blackwell)";
        default: return "NVIDIA GeForce";
    }
}

static const char* qemu_model(uint16_t dev) {
    switch (dev) {
        case 0x1111: return "QEMU VGA / bochs-display";
        case 0x1050: return "QEMU virtio-gpu";
        default:     return "QEMU display device";
    }
}

// Fill a GpuInfo from a PCI device record.
static void gpu_fill(const PciDev& p, GpuInfo& g) {
    g.vendor = p.vendor;
    g.device = p.device;
    g.bus = p.bus;
    g.dev = p.dev;
    g.func = p.func;
    g.bar0 = p.bars[0];
    g.bar0_mem = 0;
    g.kind = GPU_OTHER;
    bool io = false;
    uint64_t a = pci_bar_address(p, 0, &io);
    if (!io) g.bar0_mem = (uint32_t)(a & 0xFFFFFFFFu);
    const char* n = "display adapter";
    if (p.vendor == 0x8086) { g.kind = GPU_INTEL;  n = intel_model(p.device); }
    else if (p.vendor == 0x1002) { g.kind = GPU_AMD; n = amd_model(p.device); }
    else if (p.vendor == 0x10DE) { g.kind = GPU_NVIDIA; n = nvidia_model(p.device); }
    else if (p.vendor == 0x1AF4 && p.device == 0x1050) { g.kind = GPU_VIRTIO; n = qemu_model(p.device); }
    else if (p.vendor == 0x1234) { g.kind = GPU_BOCHS; n = qemu_model(p.device); }
    else if (p.vendor == 0x15AD) { g.kind = GPU_VMWARE; n = "VMware SVGA II"; }
    char buf[64];
    ksprintf(buf, sizeof(buf), "%s %s [%04x:%04x]", gpu_vendor_name(p.vendor), n, p.vendor, p.device);
    unsigned int i = 0;
    while (buf[i] && i < sizeof(g.name) - 1) { g.name[i] = (char)buf[i]; i++; }
    g.name[i] = 0;
}

int gpu_scan(GpuInfo* out, int max) {
    if (!out || max <= 0) return 0;
    PciDev devs[64];
    int n = pci_scan(devs, 64);
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (devs[i].class_code == 0x03) {   // display controller
            gpu_fill(devs[i], out[count++]);
        }
    }
    return count;
}

void gpu_log_adapters() {
    GpuInfo list[8];
    int n = gpu_scan(list, 8);
    if (n == 0) {
        klogf("gpu: no display adapter found on PCI bus\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        klogf("gpu: [%02x:%02x.%x] %s (bar0=0x%x)\n",
              list[i].bus, list[i].dev, list[i].func, list[i].name, (unsigned)list[i].bar0);
    }
}

// ===================== framebuffer backend state =====================
enum GpuBackend {
    GB_NONE = 0,      // nothing usable: no framebuffer at all
    GB_SOFT,          // software rendering (gfx / LVGL / swgl canvases) into the boot LFB
    GB_VIRTIO,        // virtio-gpu owns the screen via scanout resource
    GB_BOCHS,         // bochs-display BAR0 framebuffer, directly visible
#ifdef NEFU_BARE
    // Real Intel/AMD iGPU. Bare metal only: the Win32 host backend blits into a
    // DIB, never talks to a PCI display engine, and must not compile this path.
    GB_IGPU,
#endif
};

static GpuBackend s_backend = GB_NONE;
static uint8_t* s_fb = 0;
static int s_fb_w = 0, s_fb_h = 0, s_fb_pitch = 0, s_fb_bpp = 0;
static char s_adapter[56] = "";

#ifdef NEFU_BARE

// ===================== virtio-gpu (legacy) =====================
// Legacy virtio register offsets (identical to the virtio-net driver in
// core/net/net.cpp) plus the virtio-gpu control-queue protocol.
#define VGPU_DEV_FEATURES  0x00u
#define VGPU_GUEST_FEAT    0x04u
#define VGPU_QUEUE_PFN     0x08u
#define VGPU_QUEUE_SIZE    0x0Cu
#define VGPU_QUEUE_SEL     0x0Eu
#define VGPU_QUEUE_NOTIFY  0x10u
#define VGPU_STATUS        0x12u
#define VGPU_ISR           0x13u

#define VGPU_VRING_SIZE    16
#define VGPU_STATUS_ACK    0x01u
#define VGPU_STATUS_DRV    0x02u
#define VGPU_STATUS_OK     0x04u     // legacy virtio 0.9 DRIVER_OK
#define VGPU_STATUS_FAIL   0x80u

// Modern (virtio 1.0) device status bits. NOTE: the layout differs from the
// legacy 0.9 bits above: 0x04 is FAILED, FEATURES_OK is 0x08 and DRIVER_OK
// is 0x10. Using the legacy DRIVER_OK (0x04) on a modern device sets the
// FAILED bit and never asserts DRIVER_OK, which leaves the device unstarted.
#define VGP_STATUS_ACK      0x01u
#define VGP_STATUS_DRV      0x02u
#define VGP_STATUS_FAILED   0x04u
#define VGP_STATUS_FEATOK   0x08u
#define VGP_STATUS_DROK     0x10u

struct vgpu_desc {           // virtio ring descriptor (16 bytes)
    uint64_t addr;
    uint32_t len;
    uint16_t flags;          // bit0=WRITE (1: device writes into this desc), bit1=NEXT
    uint16_t next;
} __attribute__((packed));

struct vgpu_avail {          // available ring
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VGPU_VRING_SIZE];
} __attribute__((packed));

struct vgpu_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vgpu_used {
    uint16_t flags;
    uint16_t idx;
    vgpu_used_elem ring[VGPU_VRING_SIZE];
} __attribute__((packed));

static uint16_t s_vgpu_io = 0;          // legacy IO BAR base (0 = legacy absent)
static uint32_t s_vgpu_notify = 0;      // modern notify MMIO address (0 = modern absent)
static vgpu_desc* s_vgpu_desc = 0;
static vgpu_avail* s_vgpu_avail = 0;
static vgpu_used* s_vgpu_used = 0;
static uint16_t s_vgpu_qsz = VGPU_VRING_SIZE;
static uint16_t s_vgpu_last_used = 0;
static uint32_t s_vgpu_res_id = 1;      // our scanout resource id
static bool s_vgpu_active = false;

// ---- virtio-gpu protocol structures ----
struct vgpu_rect {                       // 16 bytes
    uint32_t x, y, width, height;
} __attribute__((packed));

struct vgpu_ctrl_hdr {                   // 24 bytes
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

// command ids
#define VGPU_CMD_GET_DISPLAY_INFO     0x0100u
#define VGPU_CMD_RESOURCE_CREATE_2D   0x0101u
#define VGPU_CMD_RESOURCE_FLUSH       0x0102u
#define VGPU_CMD_RESOURCE_ATTACH_BACK 0x0103u
#define VGPU_CMD_SET_SCANOUT          0x0105u
#define VGPU_CMD_TRANSFER_TO_HOST_2D  0x0107u
#define VGPU_RESP_OK_NODATA           0x1100u
#define VGPU_RESP_OK_DISPLAY_INFO     0x1101u
#define VGPU_RESP_ERR                 0x1200u
// format ids
#define VGPU_FORMAT_BGRA8888          3u

// GET_DISPLAY_INFO response: hdr + up to 16 display modes
struct vgpu_display_one {
    vgpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct vgpu_resp_display_info {
    vgpu_ctrl_hdr hdr;
    vgpu_display_one modes[16];
} __attribute__((packed));

// Submit one control-queue command and wait for the used entry.
// req/req_len: command bytes (hdr + payload) the device reads.
// resp/resp_len: buffer the device writes (at least the hdr).
static bool vgpu_cmd(const void* req, uint32_t req_len, void* resp, uint32_t resp_len) {
    if (!s_vgpu_desc) return false;   // queue must be set up (legacy or modern)
    // Two descriptors: [0] = command (device read), [1] = response buffer
    // (device WRITES into it: flags bit0 = 1).
    s_vgpu_desc[0].addr = (uint64_t)(uintptr_t)req;
    s_vgpu_desc[0].len = req_len;
    s_vgpu_desc[0].flags = 0x02u;        // NEXT (device reads the command)
    s_vgpu_desc[0].next = 1;
    s_vgpu_desc[1].addr = (uint64_t)(uintptr_t)resp;
    s_vgpu_desc[1].len = resp_len;
    s_vgpu_desc[1].flags = 0x01u;        // WRITE: device writes the response
    s_vgpu_desc[1].next = 0xFFFFu;

    uint16_t slot = s_vgpu_avail->idx % s_vgpu_qsz;
    s_vgpu_avail->ring[slot] = 0;
    s_vgpu_avail->idx++;
    klogf("gpu: submit avail_idx=%u last=%u used_ptr=0x%x avail_ptr=0x%x\n",
          (unsigned)s_vgpu_avail->idx, (unsigned)s_vgpu_last_used,
          (unsigned)(uintptr_t)s_vgpu_used, (unsigned)(uintptr_t)s_vgpu_avail);

    // Notify the device: legacy uses the IO port, modern writes the queue
    // notify MMIO word. sfence before the notify so the desc/avail writes are
    // visible before the device is kicked.
    __sync_synchronize();
    if (s_vgpu_io) {
        gp_outw((uint16_t)(s_vgpu_io + VGPU_QUEUE_NOTIFY), 0);
    } else if (s_vgpu_notify) {
        *(volatile uint32_t*)(uintptr_t)s_vgpu_notify = 0;
    }

    // Wait for the used entry by spinning in guest RAM only -- deliberately
    // no port I/O in the loop.  Every outb into 0x80 (gp_iowait) is a VM
    // exit that takes the BQL on this vCPU thread, and the virtio-gpu
    // command is processed in a bottom-half on the emulator's main loop:
    // a busy-polling guest that keeps hammering 0x80 starves that loop and
    // the command never runs.  Pure spinning generates no exits, so the
    // main loop gets the lock between our iterations and the BH fires.
    uint32_t wait = 0;
    while (wait < 2000000000u) {
        if (s_vgpu_used->idx != s_vgpu_last_used) {
            s_vgpu_last_used = s_vgpu_used->idx;
            return true;
        }
        wait++;
    }
    return false;
}

static bool vgpu_wait_ok(const vgpu_ctrl_hdr* resp, uint32_t expect_ok) {
    if (!resp) return false;
    if (resp->type == VGPU_RESP_ERR) {
        klogf("gpu: virtio-gpu cmd error (type=0x%x)\n", (unsigned)resp->type);
        return false;
    }
    if (expect_ok && resp->type != expect_ok && resp->type != VGPU_RESP_OK_NODATA)
        return false;
    return true;
}

// Initialize the virtio-gpu control queue (legacy transport).
static bool vgpu_init(uint16_t io_base) {
    s_vgpu_io = io_base;
    s_vgpu_notify = 0;
    // device status handshake (same sequence as virtio-net)
    gp_outb((uint16_t)(s_vgpu_io + VGPU_STATUS), VGPU_STATUS_ACK);
    gp_outb((uint16_t)(s_vgpu_io + VGPU_STATUS), VGPU_STATUS_ACK | VGPU_STATUS_DRV);
    uint32_t feat = gp_inl((uint16_t)(s_vgpu_io + VGPU_DEV_FEATURES));
    (void)feat;
    gp_outl((uint16_t)(s_vgpu_io + VGPU_GUEST_FEAT), 0);   // no feature bits needed

    // control queue = queue 0
    gp_outw((uint16_t)(s_vgpu_io + VGPU_QUEUE_SEL), 0);
    uint16_t qsz = gp_inw((uint16_t)(s_vgpu_io + VGPU_QUEUE_SIZE));
    if (qsz < 2) qsz = 2;
    if (qsz > VGPU_VRING_SIZE) qsz = VGPU_VRING_SIZE;
    s_vgpu_qsz = qsz;

    uint8_t* mem = (uint8_t*)kalloc(4096u * 3u + 4096u);
    if (!mem) return false;
    memset(mem, 0, 4096u * 3u + 4096u);
    uintptr_t base = (uintptr_t)mem;
    base = (base + 4095u) & ~(uintptr_t)4095u;
    s_vgpu_desc = (vgpu_desc*)base;
    s_vgpu_avail = (vgpu_avail*)(base + 4096u);
    s_vgpu_used = (vgpu_used*)(base + 8192u);
    s_vgpu_last_used = 0;
    gp_outl((uint16_t)(s_vgpu_io + VGPU_QUEUE_PFN), (uint32_t)(base >> 12));

    gp_outb((uint16_t)(s_vgpu_io + VGPU_STATUS),
            VGPU_STATUS_ACK | VGPU_STATUS_DRV | VGPU_STATUS_OK);
    s_vgpu_active = true;
    return true;
}

// ===================== virtio-gpu (modern, virtio 1.0) =====================
// QEMU 11 exposes virtio-gpu as modern-only: the transport registers live in
// PCI MMIO BARs located through the vendor capability (id 0x09). Layout:
//   common config BAR (cap type 1), notify BAR (cap type 2, with the
//   notify_off_multiplier), device config BAR (cap type 4). nefuOS identity
//   maps 0..4GB (entry.s), so the MMIO addresses are dereferenced directly.
struct VgpuModernCaps {
    uint8_t  common_bar;
    uint32_t common_off;
    uint8_t  notify_bar;
    uint32_t notify_off;
    uint32_t notify_mult;
    uint8_t  device_bar;
    uint32_t device_off;
};

static bool vgpu_find_caps(uint8_t bus, uint8_t dev, uint8_t func, VgpuModernCaps& c) {
    c.common_bar = 0xFF; c.common_off = 0;
    c.notify_bar = 0xFF; c.notify_off = 0; c.notify_mult = 4;
    c.device_bar = 0xFF; c.device_off = 0;
    uint8_t cap = pci_config_read8(bus, dev, func, 0x34);   // capabilities ptr
    while (cap && cap != 0xFF) {
        uint8_t vndr = pci_config_read8(bus, dev, func, cap);
        if (vndr == 0x09) {   // virtio PCI vendor capability
            uint8_t type = pci_config_read8(bus, dev, func, (uint8_t)(cap + 3));
            uint8_t bar  = pci_config_read8(bus, dev, func, (uint8_t)(cap + 4));
            uint32_t off = pci_config_read32(bus, dev, func, (uint8_t)(cap + 8));
            if (type == 1) { c.common_bar = bar; c.common_off = off; }
            else if (type == 2) {
                c.notify_bar = bar; c.notify_off = off;
                uint32_t m = pci_config_read32(bus, dev, func, (uint8_t)(cap + 16));
                if (m) c.notify_mult = m;
            }
            else if (type == 4) { c.device_bar = bar; c.device_off = off; }
        }
        cap = pci_config_read8(bus, dev, func, (uint8_t)(cap + 1));
    }
    return c.common_bar != 0xFF;
}

// Modern virtio common-config register offsets (virtio 1.0 spec, PCI layout).
#define VGP_C_DEVICE_FEAT_SEL 0x00u
#define VGP_C_DEVICE_FEAT     0x04u
#define VGP_C_DRIVER_FEAT_SEL 0x08u
#define VGP_C_DRIVER_FEAT     0x0Cu
#define VGP_C_DEVICE_STATUS   0x14u
#define VGP_C_QUEUE_SEL       0x16u
#define VGP_C_QUEUE_SIZE      0x18u
#define VGP_C_QUEUE_ENABLE    0x1Cu
#define VGP_C_QUEUE_NOTIFY_OFF 0x1Eu
#define VGP_C_QUEUE_DESC      0x20u
#define VGP_C_QUEUE_DRIVER    0x28u
#define VGP_C_QUEUE_DEVICE    0x30u
#define VGP_FEATURES_OK       0x08u
#define VIRTIO_F_VERSION_1    32u    // bit 32: must be negotiated

static inline uint8_t  vgp_rd8(uintptr_t base, uint32_t reg)  { return *(volatile uint8_t*)(base + reg); }
static inline uint16_t vgp_rd16(uintptr_t base, uint32_t reg) { return *(volatile uint16_t*)(base + reg); }
static inline uint32_t vgp_rd32(uintptr_t base, uint32_t reg) { return *(volatile uint32_t*)(base + reg); }
static inline uint64_t vgp_rd64(uintptr_t base, uint32_t reg) { return *(volatile uint64_t*)(base + reg); }
static inline void vgp_wr8(uintptr_t base, uint32_t reg, uint8_t v)   { *(volatile uint8_t*)(base + reg) = v; }
static inline void vgp_wr16(uintptr_t base, uint32_t reg, uint16_t v) { *(volatile uint16_t*)(base + reg) = v; }
static inline void vgp_wr32(uintptr_t base, uint32_t reg, uint32_t v) { *(volatile uint32_t*)(base + reg) = v; }
static inline void vgp_wr64(uintptr_t base, uint32_t reg, uint64_t v) { *(volatile uint64_t*)(base + reg) = v; }

static bool vgpu_init_modern(uint8_t bus, uint8_t dev, uint8_t func) {
    VgpuModernCaps c;
    if (!vgpu_find_caps(bus, dev, func, c)) {
        klogf("gpu: virtio-gpu has no modern capabilities\n");
        return false;
    }
    PciDev d = pci_read_device(bus, dev, func);
    bool io = false;
    uint64_t common_bar = pci_bar_address(d, c.common_bar, &io);
    if (io || common_bar == 0) {
        klogf("gpu: virtio-gpu common config BAR invalid\n");
        return false;
    }
    uintptr_t common = (uintptr_t)(common_bar + c.common_off);
    uintptr_t notify = 0;
    if (c.notify_bar != 0xFF) {
        uint64_t nb = pci_bar_address(d, c.notify_bar, &io);
        if (!io && nb) notify = (uintptr_t)(nb + c.notify_off);
    }

    // Device status: ACKNOWLEDGE -> DRIVER
    vgp_wr8(common, VGP_C_DEVICE_STATUS, VGPU_STATUS_ACK);
    vgp_wr8(common, VGP_C_DEVICE_STATUS, (uint8_t)(VGPU_STATUS_ACK | VGPU_STATUS_DRV));

    // What the device offers (high word bit0 = VIRTIO_F_VERSION_1, bit32).
    vgp_wr32(common, VGP_C_DEVICE_FEAT_SEL, 1);
    uint32_t dev_feat_hi = vgp_rd32(common, VGP_C_DEVICE_FEAT);
    klogf("gpu: virtio-gpu device_feat_hi=0x%x (bit0 = VERSION_1)\n", (unsigned)dev_feat_hi);

    // Feature negotiation: only VIRTIO_F_VERSION_1 (bit 32).
    vgp_wr32(common, VGP_C_DRIVER_FEAT_SEL, 1);
    vgp_wr32(common, VGP_C_DRIVER_FEAT, 1u);          // high word bit0 = bit32
    vgp_wr32(common, VGP_C_DRIVER_FEAT_SEL, 0);
    vgp_wr32(common, VGP_C_DRIVER_FEAT, 0);

    // FEATURES_OK and verify the device accepted them.
    vgp_wr8(common, VGP_C_DEVICE_STATUS,
            (uint8_t)(VGP_STATUS_ACK | VGP_STATUS_DRV | VGP_STATUS_FEATOK));
    if ((vgp_rd8(common, VGP_C_DEVICE_STATUS) & VGP_STATUS_FEATOK) == 0) {
        klogf("gpu: virtio-gpu feature negotiation rejected (status=0x%x)\n",
              (unsigned)vgp_rd8(common, VGP_C_DEVICE_STATUS));
        return false;
    }

    // Control queue = queue 0.
    vgp_wr16(common, VGP_C_QUEUE_SEL, 0);
    uint16_t qsz = vgp_rd16(common, VGP_C_QUEUE_SIZE);
    if (qsz < 2) qsz = 2;
    if (qsz > VGPU_VRING_SIZE) qsz = VGPU_VRING_SIZE;
    s_vgpu_qsz = qsz;
    uint16_t q_notify_off = vgp_rd16(common, VGP_C_QUEUE_NOTIFY_OFF);

    uint8_t* mem = (uint8_t*)kalloc(4096u * 3u + 4096u);
    if (!mem) return false;
    memset(mem, 0, 4096u * 3u + 4096u);
    uintptr_t base = (uintptr_t)mem;
    base = (base + 4095u) & ~(uintptr_t)4095u;
    s_vgpu_desc = (vgpu_desc*)base;
    s_vgpu_avail = (vgpu_avail*)(base + 4096u);
    s_vgpu_used = (vgpu_used*)(base + 8192u);
    s_vgpu_last_used = 0;

    vgp_wr64(common, VGP_C_QUEUE_DESC, (uint64_t)base);
    vgp_wr64(common, VGP_C_QUEUE_DRIVER, (uint64_t)(base + 4096u));
    vgp_wr64(common, VGP_C_QUEUE_DEVICE, (uint64_t)(base + 8192u));
    vgp_wr16(common, VGP_C_QUEUE_SIZE, s_vgpu_qsz);   // tell device our table size
    vgp_wr16(common, VGP_C_QUEUE_ENABLE, 1);
    // readback: does the device accept our queue configuration?
    klogf("gpu: qcfg desc=0x%llx drv=0x%llx dev=0x%llx size=%u en=%u\n",
          (unsigned long long)vgp_rd64(common, VGP_C_QUEUE_DESC),
          (unsigned long long)vgp_rd64(common, VGP_C_QUEUE_DRIVER),
          (unsigned long long)vgp_rd64(common, VGP_C_QUEUE_DEVICE),
          (unsigned)vgp_rd16(common, VGP_C_QUEUE_SIZE),
          (unsigned)vgp_rd16(common, VGP_C_QUEUE_ENABLE));

    vgp_wr8(common, VGP_C_DEVICE_STATUS,
            (uint8_t)(VGP_STATUS_ACK | VGP_STATUS_DRV | VGP_STATUS_FEATOK | VGP_STATUS_DROK));
    klogf("gpu: virtio-gpu status after DRIVER_OK = 0x%x\n",
          (unsigned)vgp_rd8(common, VGP_C_DEVICE_STATUS));

    s_vgpu_io = 0;
    s_vgpu_notify = notify ? (uint32_t)(notify + (uint32_t)q_notify_off * c.notify_mult) : 0;
    s_vgpu_active = true;
    klogf("gpu: virtio-gpu caps: common=bar%u+0x%x notify=bar%u+0x%x(mult=%u) device=bar%u+0x%x\n",
          (unsigned)c.common_bar, (unsigned)c.common_off,
          (unsigned)c.notify_bar, (unsigned)c.notify_off, (unsigned)c.notify_mult,
          (unsigned)c.device_bar, (unsigned)c.device_off);
    klogf("gpu: virtio-gpu common=0x%x q_notify_off=%u desc=0x%x notify=0x%x\n",
          (unsigned)common, (unsigned)q_notify_off, (unsigned)base, (unsigned)s_vgpu_notify);
    klogf("gpu: virtio-gpu modern queue ready (qsz=%u)\n", (unsigned)qsz);
    return true;
}

// Query the first enabled scanout mode from the emulated display.
static bool vgpu_get_display_info(int* w, int* h) {
    vgpu_ctrl_hdr req;
    memset(&req, 0, sizeof(req));
    req.type = VGPU_CMD_GET_DISPLAY_INFO;

    static vgpu_resp_display_info resp;
    memset(&resp, 0, sizeof(resp));
    bool cmd_ok = vgpu_cmd(&req, sizeof(req), &resp, sizeof(resp));
    klogf("gpu: get_display_info cmd=%d used_idx=%u last=%u\n",
          (int)cmd_ok, (unsigned)s_vgpu_used->idx, (unsigned)s_vgpu_last_used);
    if (cmd_ok) {
        klogf("gpu:   resp.type=0x%x modes0: x=%u y=%u w=%u h=%u en=%u\n",
              (unsigned)resp.hdr.type,
              (unsigned)resp.modes[0].r.x, (unsigned)resp.modes[0].r.y,
              (unsigned)resp.modes[0].r.width, (unsigned)resp.modes[0].r.height,
              (unsigned)resp.modes[0].enabled);
    }
    if (!cmd_ok) return false;
    if (!vgpu_wait_ok(&resp.hdr, VGPU_RESP_OK_DISPLAY_INFO)) return false;
    if (w) *w = 0;
    if (h) *h = 0;
    for (int i = 0; i < 16; i++) {
        if (resp.modes[i].enabled && resp.modes[i].r.width > 0 && resp.modes[i].r.height > 0) {
            if (w) *w = (int)resp.modes[i].r.width;
            if (h) *h = (int)resp.modes[i].r.height;
            klogf("gpu: virtio-gpu mode %d: %ux%u\n", i,
                  (unsigned)resp.modes[i].r.width, (unsigned)resp.modes[i].r.height);
            return true;
        }
    }
    klogf("gpu: virtio-gpu reported no enabled scanout\n");
    return false;
}

// Create the scanout resource, attach our framebuffer as backing, and set
// it as scanout 0. fb must stay alive for the life of the session.
static bool vgpu_set_scanout(uint8_t* fb, int w, int h) {
    uint32_t res = s_vgpu_res_id;
    uint32_t fb_bytes = (uint32_t)(w * h * 4);

    // 1. create the 2D resource
    uint8_t create[64];
    memset(create, 0, sizeof(create));
    vgpu_ctrl_hdr* ch = (vgpu_ctrl_hdr*)create;
    ch->type = VGPU_CMD_RESOURCE_CREATE_2D;
    uint32_t* cp = (uint32_t*)(create + 24);
    cp[0] = res;                       // resource_id
    cp[1] = VGPU_FORMAT_BGRA8888;      // format
    cp[2] = (uint32_t)w;               // width
    cp[3] = (uint32_t)h;               // height
    vgpu_ctrl_hdr cresp;
    memset(&cresp, 0, sizeof(cresp));
    if (!vgpu_cmd(create, 40, &cresp, sizeof(cresp))) return false;
    if (!vgpu_wait_ok(&cresp, VGPU_RESP_OK_NODATA)) return false;

    // 2. attach backing (one entry covering the whole framebuffer)
    uint8_t attach[64];
    memset(attach, 0, sizeof(attach));
    vgpu_ctrl_hdr* ah = (vgpu_ctrl_hdr*)attach;
    ah->type = VGPU_CMD_RESOURCE_ATTACH_BACK;
    uint32_t* ap = (uint32_t*)(attach + 24);
    ap[0] = res;                       // resource_id
    ap[1] = 1;                         // nr_entries
    struct VgpuMemEntry {
        uint64_t addr;
        uint32_t length;
        uint32_t pad;
    } __attribute__((packed));
    VgpuMemEntry* ent = (VgpuMemEntry*)(attach + 32);
    ent->addr = (uint64_t)(uintptr_t)fb;
    ent->length = fb_bytes;
    ent->pad = 0;
    vgpu_ctrl_hdr aresp;
    memset(&aresp, 0, sizeof(aresp));
    if (!vgpu_cmd(attach, 48, &aresp, sizeof(aresp))) return false;
    if (!vgpu_wait_ok(&aresp, VGPU_RESP_OK_NODATA)) return false;

    // 3. set scanout 0 to the resource
    uint8_t scan[64];
    memset(scan, 0, sizeof(scan));
    vgpu_ctrl_hdr* sh = (vgpu_ctrl_hdr*)scan;
    sh->type = VGPU_CMD_SET_SCANOUT;
    vgpu_rect* sr = (vgpu_rect*)(scan + 24);
    sr->x = 0; sr->y = 0; sr->width = (uint32_t)w; sr->height = (uint32_t)h;
    uint32_t* sp = (uint32_t*)(scan + 40);
    sp[0] = 0;                         // scanout_id
    sp[1] = res;                       // resource_id
    vgpu_ctrl_hdr sresp;
    memset(&sresp, 0, sizeof(sresp));
    if (!vgpu_cmd(scan, 48, &sresp, sizeof(sresp))) return false;
    if (!vgpu_wait_ok(&sresp, VGPU_RESP_OK_NODATA)) return false;

    klogf("gpu: virtio-gpu scanout ready (%ux%u, res=%u)\n", w, h, (unsigned)res);
    return true;
}

// Upload the whole framebuffer to the host and flush it to the display.
static void vgpu_present() {
    if (!s_vgpu_active || !s_fb || s_fb_w <= 0 || s_fb_h <= 0) return;

    uint8_t tr[64];
    memset(tr, 0, sizeof(tr));
    vgpu_ctrl_hdr* th = (vgpu_ctrl_hdr*)tr;
    th->type = VGPU_CMD_TRANSFER_TO_HOST_2D;
    vgpu_rect* rr = (vgpu_rect*)(tr + 24);
    rr->x = 0; rr->y = 0; rr->width = (uint32_t)s_fb_w; rr->height = (uint32_t)s_fb_h;
    uint32_t* tp = (uint32_t*)(tr + 40);
    tp[0] = 0;                         // offset
    tp[1] = s_vgpu_res_id;             // resource_id
    tp[2] = 0;                         // padding
    vgpu_ctrl_hdr tresp;
    memset(&tresp, 0, sizeof(tresp));
    if (!vgpu_cmd(tr, 56, &tresp, sizeof(tresp))) return;
    if (!vgpu_wait_ok(&tresp, VGPU_RESP_OK_NODATA)) return;

    uint8_t fl[64];
    memset(fl, 0, sizeof(fl));
    vgpu_ctrl_hdr* fh = (vgpu_ctrl_hdr*)fl;
    fh->type = VGPU_CMD_RESOURCE_FLUSH;
    vgpu_rect* fr = (vgpu_rect*)(fl + 24);
    fr->x = 0; fr->y = 0; fr->width = (uint32_t)s_fb_w; fr->height = (uint32_t)s_fb_h;
    uint32_t* fp = (uint32_t*)(fl + 40);
    fp[0] = s_vgpu_res_id;             // resource_id
    fp[1] = 0;                         // padding
    vgpu_ctrl_hdr fresp;
    memset(&fresp, 0, sizeof(fresp));
    if (!vgpu_cmd(fl, 48, &fresp, sizeof(fresp))) return;
    (void)vgpu_wait_ok(&fresp, VGPU_RESP_OK_NODATA);
}

// ===================== bochs-display =====================
// QEMU bochs-display exposes the raw framebuffer as a memory BAR. The
// resolution is fixed by the device (default 1024x768x32, xres/yres props).
static bool bochs_setup(const GpuInfo& g, uint8_t** fb, int* w, int* h, int* pitch, int* bpp) {
    if (g.bar0_mem == 0) {
        klogf("gpu: bochs-display has no memory BAR0\n");
        return false;
    }
    pci_enable_busmaster(PciDev{g.bus, g.dev, g.func, g.vendor, g.device,
                                0x03, 0x00, 0x00, {g.bar0,0,0,0,0,0}, 0, true});
    int bw = 1024, bh = 768;
    if (g.device == 0x1111 && g.vendor == 0x1234) {
        // stdvga (VBE path already handled in boot.s) reports the same ID;
        // bochs-display default is 1024x768x32. Use the boot resolution when
        // it looks valid, otherwise fall back to the device default.
        if (*w > 0 && *h > 0) { bw = *w; bh = *h; }
    }
    *fb = (uint8_t*)(uintptr_t)g.bar0_mem;
    *w = bw;
    *h = bh;
    *pitch = bw * 4;
    *bpp = 32;
    klogf("gpu: bochs-display framebuffer at 0x%x (%dx%dx32)\n",
          (unsigned)g.bar0_mem, bw, bh);
    return true;
}

// ===================== real hardware: Intel / AMD integrated GPU =====================
// On real hardware the firmware (UEFI GOP / VBE) has already programmed the
// display engine, and the boot loader handed us the framebuffer that resulted.
// There is no command queue to drive here, so "taking the screen over" means:
//   1. locate the GPU MMIO register window on PCI,
//   2. read back the live pipe/plane state (source size, stride, surface base),
//   3. use it to prove the boot framebuffer is the surface the hardware is
//      really scanning out, and to correct the geometry when the boot info was
//      stale - a wrong pitch is the classic source of skewed / torn output.
// The pipe is never reprogrammed: writing a wrong surface into the plane base
// on real hardware means a black screen, and the firmware image is already
// correct in every case we can verify.
// Every MMIO access is range checked first: nefuOS identity maps only 0..4GB,
// so a BAR placed above 4GB would fault and such a window is skipped instead.
// Intel display engine registers (display MMIO lives inside the GTTMMADR BAR0
// window; offsets are the legacy DSP/PIPE set used from Gen2 through Gen12).
#define IGPU_PIPE_SRC(p)      (0x6001Cu + (uint32_t)(p) * 0x1000u)  // A 0x6001C B 0x6101C
#define IGPU_PLANE_CTRL(p)    (0x70180u + (uint32_t)(p) * 0x1000u)  // bit31 = plane enable
#define IGPU_PLANE_STRIDE(p)  (0x70188u + (uint32_t)(p) * 0x1000u)  // bytes, bits[15:0]
#define IGPU_PLANE_SURF(p)    (0x7019Cu + (uint32_t)(p) * 0x1000u)  // surface base
// AMD display controller (DCE/DCN) plane registers. These moved between
// generations: DCE 6+ keeps the primary surface at 0x6118 with the pitch at
// 0x6128, the older DCE parts put the surface at 0x6110. Rather than trusting
// one revision, the probe reads every candidate and accepts only the one whose
// surface address really is the boot framebuffer.
#define AMD_D1GRPH_ENABLE     0x6100u   // bit0 = plane enable
#define AMD_D1GRPH_CONTROL    0x6104u   // bit0 = format/enable on some parts
#define AMD_D1GRPH_X_START    0x6108u
#define AMD_D1GRPH_Y_START    0x610Cu
#define AMD_D1GRPH_X_END      0x6110u
#define AMD_D1GRPH_Y_END      0x6114u
#define AMD_D1GRPH_PITCH      0x6128u

struct IgpuPipe {
    int       id;         // 0..3 for pipes A..D
    bool      enabled;
    bool      surf_verified;   // plane surface proven to be the boot framebuffer
    const char* surf_mode;     // "physical" or "ggtt"
    uint32_t  w, h;
    uint32_t  stride;     // bytes
    uint64_t  surf;       // surface base address as the register reports it
};

struct IgpuHw {
    bool     mmio_ok;
    uint32_t mmio;
    IgpuPipe best;
};

// What the boot loader handed us is the address the CPU writes pixels to. On
// Intel that is the aperture (GMADR) view of the surface while DSPADDR holds
// either that same physical page (gen2-4) or a GGTT offset (gen5+), so both
// forms have to be checked before a takeover may be claimed.
static inline uint32_t igpu_page(uint64_t a) {
    return (uint32_t)(a & 0xFFFFF000ull);
}

// GGTT page table entries sit at the start of the GTTMMADR window; the register
// block begins at 0x60000, so only offsets that stay below it are translated.
// A 64-bit PTE per 4KB page: present bit 0, physical page in bits 51:12.
static bool igpu_ggtt_to_phys(uint32_t mmio, uint64_t ggtt_off, uint64_t* phys) {
    if ((ggtt_off & 0xFFFull) != 0) return false;
    uint64_t pte_off = (ggtt_off >> 12) * 8ull;
    if (pte_off + 8ull > 0x60000ull) return false;      // would read registers
    uint64_t pte = *(volatile uint64_t*)(uintptr_t)(mmio + (uint32_t)pte_off);
    if ((pte & 1ull) == 0) return false;                // not present
    *phys = pte & 0x000FFFFFFFFFF000ull;
    return true;
}

static inline bool igpu_mmio_usable(uint64_t a) {
    return a != 0 && a < 0x100000000ull && (a & 0xFFFu) == 0;
}

static inline uint32_t igpu_rd32(uint32_t base, uint32_t off) {
    return *(volatile uint32_t*)(uintptr_t)(base + off);
}

// A pipe is only trusted when its geometry is inside sane monitor ranges and
// the stride is a plausible byte pitch for that width.
static bool igpu_pipe_sane(const IgpuPipe& p) {
    if (!p.enabled) return false;
    if (p.w < 320 || p.w > 8192 || p.h < 200 || p.h > 8192) return false;
    if (p.stride != 0 && (p.stride < p.w * 2 || p.stride > p.w * 8 || p.stride > 0x8000u))
        return false;
    return true;
}

// Intel: DSPADDR carries a GGTT offset from gen5 on and a plain physical page on
// the older parts. Either form is accepted, but only when it resolves to the page
// the boot loader handed us - which is what makes the read trustworthy at all.
static bool igpu_intel_probe(uint8_t bus, uint8_t dev, uint8_t func, uint32_t want_page,
                             IgpuHw& hw) {
    PciDev d = pci_read_device(bus, dev, func);
    bool io = false;
    uint64_t mmio = pci_bar_address(d, 0, &io);        // GTTMMADR
    memset(&hw, 0, sizeof(hw));
    klogf("gpu: intel iGPU %02x:%02x.%x bar0=0x%llx\n",
          bus, dev, func, (unsigned long long)mmio);
    if (io || !igpu_mmio_usable(mmio)) {
        klogf("gpu: intel iGPU MMIO not usable (0..4GB identity map), keeping boot LFB\n");
        return false;
    }
    hw.mmio_ok = true;
    hw.mmio = (uint32_t)mmio;

    IgpuPipe widest;
    memset(&widest, 0, sizeof(widest));
    for (int p = 0; p < 4; p++) {
        IgpuPipe c;
        memset(&c, 0, sizeof(c));
        c.id = p;
        uint32_t src  = igpu_rd32(hw.mmio, IGPU_PIPE_SRC(p));
        uint32_t ctrl = igpu_rd32(hw.mmio, IGPU_PLANE_CTRL(p));
        uint32_t strd = igpu_rd32(hw.mmio, IGPU_PLANE_STRIDE(p));
        uint32_t surf = igpu_rd32(hw.mmio, IGPU_PLANE_SURF(p));
        c.enabled = (ctrl & 0x80000000u) != 0;
        c.w = (src & 0xFFFFu) + 1u;
        c.h = ((src >> 16) & 0xFFFFu) + 1u;
        c.stride = strd & 0xFFFFu;
        c.surf = (uint64_t)(surf & 0xFFFFF000u);
        klogf("gpu: intel pipe %c src=%ux%u ctrl=0x%x stride=%u surf=0x%x\n",
              (char)('A' + p), (unsigned)c.w, (unsigned)c.h,
              (unsigned)ctrl, (unsigned)c.stride, (unsigned)(uint32_t)c.surf);
        if (!igpu_pipe_sane(c)) continue;

        if ((uint32_t)c.surf == want_page) {
            c.surf_verified = true;
            c.surf_mode = "physical";
        } else {
            uint64_t phys = 0;
            if (igpu_ggtt_to_phys(hw.mmio, c.surf, &phys) && igpu_page(phys) == want_page) {
                c.surf_verified = true;
                c.surf_mode = "ggtt";
                klogf("gpu: intel pipe %c GGTT 0x%x -> phys 0x%llx, that is the boot LFB\n",
                      (char)('A' + p), (unsigned)(uint32_t)c.surf,
                      (unsigned long long)phys);
            }
        }
        if (c.surf_verified) {
            if (!hw.best.surf_verified || c.w > hw.best.w) hw.best = c;
        } else if (!widest.enabled || c.w > widest.w) {
            widest = c;
        }
    }
    if (!hw.best.surf_verified && widest.enabled) {
        klogf("gpu: intel widest sane pipe %c is %ux%u surf=0x%x, the boot LFB page is 0x%x\n",
              (char)('A' + widest.id), (unsigned)widest.w, (unsigned)widest.h,
              (unsigned)(uint32_t)widest.surf, (unsigned)want_page);
    }
    return hw.best.surf_verified;
}

// AMD candidate register sets, tried in order. The surface address is the only
// field accepted blindly, and only when it equals the boot framebuffer, because
// the per-generation offsets disagree and a wrong window reads garbage that
// still looks like a number.
struct AmdCand {
    uint32_t surf_lo, surf_hi, pitch, x_start, y_start, x_end, y_end;
};

static const AmdCand s_amd_cands[2] = {
    // DCE 6 .. DCN: primary surface 0x6118/0x611C, pitch 0x6128
    {0x6118u, 0x611Cu, AMD_D1GRPH_PITCH, AMD_D1GRPH_X_START, AMD_D1GRPH_Y_START,
     AMD_D1GRPH_X_END, AMD_D1GRPH_Y_END},
    // revisions that keep the pitch one dword lower; same surface window
    {0x6118u, 0x611Cu, 0x6120u, AMD_D1GRPH_X_START, AMD_D1GRPH_Y_START,
     AMD_D1GRPH_X_END, AMD_D1GRPH_Y_END},
    // Pre-DCE6 parts (M56-class) put the primary surface at 0x6110, but on the
    // newer layouts that address is X_END, so probing it here would mean reading
    // a window register as a surface. Not guessed: such a machine will log the
    // three registers below and get a layout added then.
};

static bool igpu_amd_probe(uint8_t bus, uint8_t dev, uint8_t func, uint32_t want_page,
                           IgpuHw& hw) {
    PciDev d = pci_read_device(bus, dev, func);
    bool io = false;
    uint64_t mmio = pci_bar_address(d, 0, &io);        // BAR0 = display MMIO
    memset(&hw, 0, sizeof(hw));
    klogf("gpu: amd iGPU %02x:%02x.%x bar0=0x%llx\n",
          bus, dev, func, (unsigned long long)mmio);
    if (io || !igpu_mmio_usable(mmio)) {
        klogf("gpu: amd iGPU MMIO not usable (0..4GB identity map), keeping boot LFB\n");
        return false;
    }
    hw.mmio_ok = true;
    hw.mmio = (uint32_t)mmio;

    // D1GRPH_ENABLE bit0 is authoritative; some revisions only set the format
    // enable inside D1GRPH_CONTROL, so either bit proves the plane is live.
    uint32_t en   = igpu_rd32(hw.mmio, AMD_D1GRPH_ENABLE);
    uint32_t ctrl = igpu_rd32(hw.mmio, AMD_D1GRPH_CONTROL);
    klogf("gpu: amd plane A enable=0x%x control=0x%x\n", (unsigned)en, (unsigned)ctrl);
    if (((en | ctrl) & 0x1u) == 0) {
        klogf("gpu: amd plane A is disabled, keeping boot LFB\n");
        return false;
    }

    for (int k = 0; k < (int)(sizeof(s_amd_cands) / sizeof(s_amd_cands[0])); k++) {
        const AmdCand& q = s_amd_cands[k];
        uint32_t lo = igpu_rd32(hw.mmio, q.surf_lo);
        uint32_t hi = igpu_rd32(hw.mmio, q.surf_hi);
        uint32_t xs = igpu_rd32(hw.mmio, q.x_start);
        uint32_t ys = igpu_rd32(hw.mmio, q.y_start);
        uint32_t xe = igpu_rd32(hw.mmio, q.x_end);
        uint32_t ye = igpu_rd32(hw.mmio, q.y_end);
        uint32_t pitch = igpu_rd32(hw.mmio, q.pitch) & 0x3FFFu;
        uint64_t surf = ((uint64_t)hi << 32) | (uint64_t)(lo & 0xFFFFF000u);
        klogf("gpu: amd cand%d surf=0x%llx window=(%u,%u)-(%u,%u) pitch=%u\n",
              k, (unsigned long long)surf, (unsigned)xs, (unsigned)ys,
              (unsigned)xe, (unsigned)ye, (unsigned)pitch);
        if (igpu_page(surf) != want_page) continue;

        IgpuPipe c;
        memset(&c, 0, sizeof(c));
        c.id = 0;
        c.enabled = true;
        c.surf = surf;
        c.surf_verified = true;
        c.surf_mode = "physical";
        // X_END/Y_END are inclusive, so the window is end - start + 1: reading
        // them as plain widths is what made the old code reject every pipe.
        c.w = (xe >= xs) ? (xe - xs + 1u) : 0u;
        c.h = (ye >= ys) ? (ye - ys + 1u) : 0u;
        c.stride = pitch;
        if (!igpu_pipe_sane(c)) {
            klogf("gpu: amd cand%d surface matches but window %ux%u pitch=%u is unusable\n",
                  k, (unsigned)c.w, (unsigned)c.h, (unsigned)c.stride);
            continue;
        }
        hw.best = c;
        return true;
    }
    klogf("gpu: amd no candidate surface equals the boot LFB page 0x%x, keeping boot LFB\n",
          (unsigned)want_page);
    return false;
}

static void gpu_set_adapter(const char* name) {
    unsigned int i = 0;
    while (name[i] && i < sizeof(s_adapter) - 1) { s_adapter[i] = name[i]; i++; }
    s_adapter[i] = 0;
}

// Software fallback proof. Every rendering path that does not own hardware
// (gfx, LVGL, the window manager, and the swgl canvases the browser composites)
// writes into the boot framebuffer, so that memory has to be proven writable
// instead of assumed. Two pixels are touched and restored immediately.
static bool gpu_lfb_writable(uint8_t* fb, int w, int h, int pitch) {
    if (!fb || w <= 0 || h <= 0 || pitch <= 0) return false;
    if (w > 8192 || h > 8192) return false;
    volatile uint32_t* p0 = (volatile uint32_t*)(void*)fb;
    volatile uint32_t* p1 = (volatile uint32_t*)(void*)(fb + (uintptr_t)pitch);
    uint32_t a = *p0;
    uint32_t b = *p1;
    *p0 = 0x00FF00FFu;
    *p1 = 0x0000FF00u;
    __sync_synchronize();
    bool ok = (*p0 == 0x00FF00FFu) && (*p1 == 0x0000FF00u);
    *p0 = a;
    *p1 = b;
    __sync_synchronize();
    return ok;
}

// Verify the live hardware state against the boot framebuffer and refine the
// geometry. The surface address is the proof, but it has to be resolved first:
// Intel stores a GGTT offset (translated here through the PTE window), AMD a
// physical page. Only a resolved match counts, and only then is the register
// read trusted enough to override the boot pitch.
static bool igpu_takeover(const GpuInfo& g, uint8_t** fb, int* w, int* h, int* pitch, int* bpp) {
    if (!fb || !*fb || !w || !h || !pitch || !bpp) return false;
    uintptr_t bootfb = (uintptr_t)*fb;
    uint32_t want_page = igpu_page((uint64_t)bootfb);

    IgpuHw hw;
    memset(&hw, 0, sizeof(hw));
    bool probed = false;
    if (g.vendor == 0x8086)      probed = igpu_intel_probe(g.bus, g.dev, g.func, want_page, hw);
    else if (g.vendor == 0x1002) probed = igpu_amd_probe(g.bus, g.dev, g.func, want_page, hw);
    if (!probed || !hw.best.enabled || !hw.best.surf_verified) {
        klogf("gpu: %s surface is not the boot LFB, keeping boot framebuffer\n",
              g.vendor == 0x8086 ? "intel" : "amd");
        return false;
    }

    klogf("gpu: %s pipe %c surface=0x%x matches boot LFB 0x%x (%s addressing)\n",
          g.vendor == 0x8086 ? "intel" : "amd", (char)('A' + hw.best.id),
          (unsigned)(uint32_t)hw.best.surf, (unsigned)(uint32_t)bootfb,
          hw.best.surf_mode ? hw.best.surf_mode : "physical");

    // Geometry correction. Width/height from the pipe source size, pitch from
    // the plane stride, both only when they are self-consistent.
    int nw = (int)hw.best.w;
    int nh = (int)hw.best.h;
    int npitch = (int)hw.best.stride;
    if (nw < 320 || nw > 8192 || nh < 200 || nh > 8192) {
        nw = *w; nh = *h;
    }
    if (npitch < nw * 2 || npitch > nw * 8) npitch = 0;

    bool changed = (nw != *w || nh != *h || (npitch > 0 && npitch != *pitch));
    *w = nw; *h = nh;
    if (npitch > 0) *pitch = npitch;
    (void)bpp;

    klogf("gpu: %s iGPU owns the screen (%dx%d pitch=%d%s)\n",
          g.vendor == 0x8086 ? "intel" : "amd", *w, *h, *pitch,
          changed ? ", geometry corrected from boot info" : "");
    return true;
}

#endif // NEFU_BARE

// ===================== public entry points =====================

bool gpu_owns_screen() {
    // Only a backend that drives the display hardware counts as a takeover.
    // GB_SOFT still renders, but through software into the boot LFB.
#ifdef NEFU_BARE
    return s_backend == GB_VIRTIO || s_backend == GB_BOCHS || s_backend == GB_IGPU;
#else
    // Host: there is no display backend at all (the Win32 surface is ours), so
    // nothing here may claim ownership, and GB_IGPU does not even exist.
    return false;
#endif
}

void gpu_present() {
#ifdef NEFU_BARE
    if (s_backend == GB_VIRTIO) vgpu_present();
    else if (s_backend == GB_IGPU) {
        // Real hardware scans the framebuffer straight out of system memory, so
        // there is nothing to upload. The fence only makes sure the pixels we
        // just wrote are ordered before the display engine samples them again.
        __sync_synchronize();
    }
    // GB_BOCHS / GB_NONE: framebuffer is directly visible, nothing to do.
#endif
}

bool gpu_setup_framebuffer(uint8_t** fb, int* w, int* h, int* pitch, int* bpp) {
    if (!fb || !w || !h || !pitch || !bpp) return false;
    s_fb = *fb; s_fb_w = *w; s_fb_h = *h; s_fb_pitch = *pitch; s_fb_bpp = *bpp;

#ifdef NEFU_BARE
    GpuInfo list[8];
    int n = gpu_scan(list, 8);
    // No adapter is not an error: the software fallback at the end of this
    // function reports the outcome once, for every path.
    // Prefer virtio-gpu, then bochs-display; anything else keeps the boot LFB.
    for (int i = 0; i < n; i++) {
        if (list[i].kind == GPU_VIRTIO) {
            // Transport: legacy first (IO BAR), then modern (MMIO caps).
            bool inited = false;
            {
                bool is_io = false;
                uint64_t a = pci_bar_address(PciDev{list[i].bus, list[i].dev, list[i].func,
                                                    list[i].vendor, list[i].device,
                                                    0x03, 0x00, 0x00, {list[i].bar0,0,0,0,0,0}, 0, true},
                                             0, &is_io);
                if (is_io) {
                    uint16_t io = (uint16_t)(a & 0xFFFCu);
                    if (io) inited = vgpu_init(io);
                }
            }
            if (!inited) {
                inited = vgpu_init_modern(list[i].bus, list[i].dev, list[i].func);
            }
            if (!inited) {
                klogf("gpu: virtio-gpu init failed (legacy + modern)\n");
                continue;
            }
            int gw = 0, gh = 0;
            if (!vgpu_get_display_info(&gw, &gh) || gw <= 0 || gh <= 0) {
                klogf("gpu: virtio-gpu no display info, falling back to boot LFB\n");
                continue;
            }
            // framebuffer: allocate the scanout backing (page aligned),
            // fill it black, then set the scanout.
            uint32_t bytes = (uint32_t)(gw * gh * 4);
            uint8_t* raw = (uint8_t*)kalloc(bytes + 4096);
            if (!raw) { klogf("gpu: virtio-gpu fb alloc failed\n"); continue; }
            uint8_t* fba = (uint8_t*)(((uintptr_t)raw + 4095u) & ~(uintptr_t)4095u);
            memset(fba, 0, bytes);
            if (!vgpu_set_scanout(fba, gw, gh)) {
                klogf("gpu: virtio-gpu set_scanout failed\n");
                kfree(raw);
                continue;
            }
            s_backend = GB_VIRTIO;
            s_fb = fba;
            s_fb_w = gw; s_fb_h = gh; s_fb_pitch = gw * 4; s_fb_bpp = 32;
            *fb = fba; *w = gw; *h = gh; *pitch = gw * 4; *bpp = 32;
            gpu_set_adapter(list[i].name);
            klogf("gpu: virtio-gpu active, screen %dx%dx32 fb=0x%x\n", gw, gh, (unsigned)(uintptr_t)fba);
            return true;
        }
    }
    // bochs-display / stdvga: the adapter exposes the real framebuffer as
    // BAR0, so the driver takes the screen over from PCI instead of trusting
    // the boot loader. When the boot LFB is already mapped at the same
    // address (the normal case for QEMU VGA), the takeover is seamless; if
    // the addresses disagree we keep the boot LFB rather than write into a
    // window the host is not scanning out.
    for (int i = 0; i < n; i++) {
        if (list[i].kind == GPU_BOCHS && list[i].vendor == 0x1234 && list[i].device == 0x1111) {
            uint8_t* bar0fb = (uint8_t*)(uintptr_t)list[i].bar0_mem;
            if (bar0fb == 0) {
                klogf("gpu: bochs-display BAR0 absent, keeping boot LFB\n");
                continue;
            }
            if (*fb != 0 && (uintptr_t)*fb != (uintptr_t)bar0fb) {
                klogf("gpu: bochs-display BAR0 0x%x != boot LFB 0x%x, keeping boot LFB\n",
                      (unsigned)(uintptr_t)bar0fb, (unsigned)(uintptr_t)*fb);
                continue;
            }
            if (bochs_setup(list[i], fb, w, h, pitch, bpp)) {
                s_backend = GB_BOCHS;
                s_fb = *fb; s_fb_w = *w; s_fb_h = *h; s_fb_pitch = *pitch; s_fb_bpp = *bpp;
                gpu_set_adapter(list[i].name);
                klogf("gpu: bochs-display owns the screen (BAR0)\n");
                return true;
            }
        }
    }
    // Real hardware: Intel / AMD integrated GPU. The firmware already owns the
    // scanout, so we verify the live pipe against the boot framebuffer and refine
    // the geometry instead of reprogramming the display engine.
    for (int i = 0; i < n; i++) {
        if (list[i].kind != GPU_INTEL && list[i].kind != GPU_AMD) continue;
        if (igpu_takeover(list[i], fb, w, h, pitch, bpp)) {
            s_backend = GB_IGPU;
            s_fb = *fb; s_fb_w = *w; s_fb_h = *h; s_fb_pitch = *pitch; s_fb_bpp = *bpp;
            gpu_set_adapter(list[i].name);
            return true;
        }
    }
    // Nothing took the screen over: fall back to software rendering into the
    // boot framebuffer. That is the path gfx, LVGL, the window manager and the
    // swgl canvases the browser composites all draw through - none of it needs
    // GPU MMIO, so a BAR above the 4GB identity map does not disable rendering,
    // it only disables hardware takeover. Prove the memory is writable before
    // advertising it instead of assuming the boot info block was right.
    if (*fb && gpu_lfb_writable(*fb, *w, *h, *pitch)) {
        s_backend = GB_SOFT;
        s_fb = *fb; s_fb_w = *w; s_fb_h = *h; s_fb_pitch = *pitch; s_fb_bpp = *bpp;
        klogf("gpu: software rendering on boot LFB 0x%x (%dx%dx%d), verified writable\n",
              (unsigned)(uintptr_t)*fb, *w, *h, *bpp ? *bpp : 32);
    } else {
        klogf("gpu: no usable framebuffer (LFB 0x%x %dx%dx%d) - screen unavailable\n",
              (unsigned)(uintptr_t)*fb, *w, *h, *bpp ? *bpp : 32);
        return false;
    }
#else
    (void)fb; (void)w; (void)h; (void)pitch; (void)bpp;
#endif
    return true;
}

} // namespace drivers
} // namespace nefu
