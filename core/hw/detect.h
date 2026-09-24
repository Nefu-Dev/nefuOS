// nefuOS Hardware Detection
// - USB devices (flash drives, hubs)
// - Storage devices (HDD, SSD, USB drives)
// - Network adapters
// - Display adapters
// - Input devices (keyboard, mouse)
// - CPU info

#pragma once

#include "../klib/klib.h"
#include <cstdint>

namespace nefu {
namespace hw {

// Device types
enum DeviceType {
    DEVICE_UNKNOWN = 0,
    DEVICE_CPU,
    DEVICE_MEMORY,
    DEVICE_STORAGE,     // HDD/SSD/USB drive
    DEVICE_USB,         // USB device
    DEVICE_DISPLAY,     // Graphics card
    DEVICE_NETWORK,     // Network adapter
    DEVICE_INPUT,       // Keyboard/mouse
    DEVICE_AUDIO,       // Sound card
    DEVICE_BUS,         // PCI bus
};

// Storage bus types
enum BusType {
    BUS_UNKNOWN = 0,
    BUS_IDE,
    BUS_SATA,
    BUS_SCSI,
    BUS_USB,
    BUS_NVME,
};

// Device info structure
struct DeviceInfo {
    DeviceType type;
    BusType bus;
    uint16_t vendor_id;     // PCI vendor ID
    uint16_t product_id;    // PCI product ID
    uint8_t  class_id;      // PCI class
    uint8_t  subclass_id;   // PCI subclass
    char name[64];          // Device name
    char vendor_name[32];   // Vendor name
    uint64_t size_bytes;    // Storage size (if storage)
    uint32_t lba_start;     // Start LBA (for partitions)
    uint32_t lba_count;     // Number of sectors
    bool removable;         // Removable media (USB)
    bool mounted;           // Currently mounted
    char mount_point[32];   // Mount path
};

// Max devices
const int MAX_DEVICES = 64;

// Device list
extern int g_device_count;
extern DeviceInfo g_devices[MAX_DEVICES];

// USB device class codes
const uint8_t USB_CLASS_AUDIO    = 0x01;
const uint8_t USB_CLASS_COMM     = 0x02;
const uint8_t USB_CLASS_HID      = 0x03;  // Keyboard/mouse
const uint8_t USB_CLASS_HUB       = 0x09;
const uint8_t USB_CLASS_STORAGE  = 0x08;  // USB flash drive
const uint8_t USB_CLASS_PRINTER  = 0x07;

// USB endpoint types
const uint8_t USB_EP_CONTROL     = 0x00;
const uint8_t USB_EP_ISOCHRONOUS = 0x01;
const uint8_t USB_EP_BULK        = 0x02;
const uint8_t USB_EP_INTERRUPT   = 0x03;

// USB standard requests
const uint8_t USB_REQ_GET_STATUS     = 0x00;
const uint8_t USB_REQ_CLEAR_FEATURE  = 0x01;
const uint8_t USB_REQ_SET_FEATURE   = 0x03;
const uint8_t USB_REQ_SET_ADDRESS   = 0x05;
const uint8_t USB_REQ_GET_DESCRIPTOR = 0x06;
const uint8_t USB_REQ_SET_DESCRIPTOR = 0x07;
const uint8_t USB_REQ_GET_CONFIG    = 0x08;
const uint8_t USB_REQ_SET_CONFIG    = 0x09;

// USB descriptor types
const uint8_t USB_DESC_DEVICE    = 0x01;
const uint8_t USB_DESC_CONFIG    = 0x02;
const uint8_t USB_DESC_STRING    = 0x03;
const uint8_t USB_DESC_INTERFACE = 0x04;
const uint8_t USB_DESC_ENDPOINT  = 0x05;

// USB device descriptor
struct USBDeviceDescriptor {
    uint8_t  length;
    uint8_t  desc_type;
    uint16_t usb_version;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t  manufacturer_str;
    uint8_t  product_str;
    uint8_t  serial_str;
    uint8_t  num_configurations;
} __attribute__((packed));

// USB configuration descriptor
struct USBConfigDescriptor {
    uint8_t  length;
    uint8_t  desc_type;
    uint16_t total_length;
    uint8_t  num_interfaces;
    uint8_t  config_value;
    uint8_t  config_str;
    uint8_t  attributes;
    uint8_t  max_power;
} __attribute__((packed));

// USB interface descriptor
struct USBInterfaceDescriptor {
    uint8_t  length;
    uint8_t  desc_type;
    uint8_t  interface_number;
    uint8_t  alternate_setting;
    uint8_t  num_endpoints;
    uint8_t  interface_class;
    uint8_t  interface_subclass;
    uint8_t  interface_protocol;
    uint8_t  interface_str;
} __attribute__((packed));

// USB endpoint descriptor
struct USBEndpointDescriptor {
    uint8_t  length;
    uint8_t  desc_type;
    uint8_t  endpoint_address;
    uint8_t  attributes;
    uint16_t max_packet_size;
    uint8_t  interval;
} __attribute__((packed));

// USB mass storage bulk-only transport (BBB) command block
struct CBW {
    uint32_t signature;     // 0x43425355 ("USBC")
    uint32_t tag;
    uint32_t data_len;
    uint8_t  flags;         // bit 7: direction (1=in, 0=out)
    uint8_t  lun;
    uint8_t  cb_len;        // CBWCB length
    uint8_t  cb[16];        // Command block
} __attribute__((packed));

// USB mass storage command status wrapper
struct CSW {
    uint32_t signature;     // 0x53425355 ("USBS")
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
} __attribute__((packed));

// SCSI commands
const uint8_t SCSI_TEST_UNIT_READY = 0x00;
const uint8_t SCSI_REQUEST_SENSE   = 0x03;
const uint8_t SCSI_INQUIRY        = 0x12;
const uint8_t SCSI_READ_CAPACITY   = 0x25;
const uint8_t SCSI_READ_10        = 0x28;
const uint8_t SCSI_WRITE_10       = 0x2A;

// SCSI inquiry response
struct SCSIInquiry {
    uint8_t  peri_qual_type;    // Peripheral qualifier + device type
    uint8_t  rmb;               // Removable media bit
    uint8_t  version;
    uint8_t  resp_data_format;
    uint8_t  additional_len;
    uint8_t  reserved[3];
    uint8_t  vendor_id[8];
    uint8_t  product_id[16];
    uint8_t  product_rev[4];
} __attribute__((packed));

// SCSI read capacity response
struct SCSIReadCapacity {
    uint32_t last_lba;
    uint32_t block_size;
} __attribute__((packed));

// PCI device (for enumeration)
struct PCIDevice {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  function;
};

// CPU info
struct CPUInfo {
    char vendor[13];        // "GenuineIntel", "AuthenticAMD", etc.
    char model_name[49];    // Full CPU model name
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t max_cores;
    uint32_t max_threads;
    uint64_t features;      // CPUID feature flags
    uint32_t clock_mhz;
};

// Memory info
struct MemoryInfo {
    uint64_t total_ram;     // Total RAM
    uint64_t free_ram;      // Free RAM
    uint64_t used_ram;      // Used RAM
    uint64_t reserved_ram;  // Reserved by firmware
    uint32_t total_pages;   // Total 4KB pages
    uint32_t free_pages;    // Free 4KB pages
};

// PCI configuration space access
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

// Enumerate PCI buses
int pci_enumerate(PCIDevice* out, int max) {
    int count = 0;
    for (uint8_t bus = 0; bus < 256 && count < max; bus++) {
        for (uint8_t slot = 0; slot < 32 && count < max; slot++) {
            for (uint8_t func = 0; func < 8 && count < max; func++) {
                uint32_t id = pci_read_config(bus, slot, func, 0);
                uint16_t vendor = id & 0xFFFF;
                uint16_t device = (id >> 16) & 0xFFFF;
                if (vendor == 0xFFFF) continue;  // No device

                uint32_t class_reg = pci_read_config(bus, slot, func, 0x08);
                PCIDevice* dev = &out[count++];
                dev->vendor_id = vendor;
                dev->device_id = device;
                dev->class_code = (class_reg >> 24) & 0xFF;
                dev->subclass = (class_reg >> 16) & 0xFF;
                dev->prog_if = (class_reg >> 8) & 0xFF;
                dev->bus = bus;
                dev->slot = slot;
                dev->function = func;

                // Skip if multi-function and not first function
                if (func == 0) {
                    uint8_t header = (pci_read_config(bus, slot, 0, 0x0C) >> 16) & 0x80;
                    if (!header) break;  // Not multi-function
                }
            }
        }
    }
    return count;
}

// Get device name from PCI ID
const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01:  // Mass storage controller
            switch (subclass) {
                case 0x01: return "IDE Controller";
                case 0x06: return "SATA Controller";
                case 0x08: return "Mass Storage Controller";
                default: return "Storage Controller";
            }
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x08: return "Base Peripheral";
        case 0x0C: return "Serial Bus Controller";
        default: return "Unknown";
    }
}

// Detect all hardware
void detect_all() {
    g_device_count = 0;

    // Enumerate PCI devices
    PCIDevice pci_devs[32];
    int n = pci_enumerate(pci_devs, 32);

    for (int i = 0; i < n && g_device_count < MAX_DEVICES; i++) {
        PCIDevice* p = &pci_devs[i];
        DeviceInfo* dev = &g_devices[g_device_count++];

        dev->type = DEVICE_UNKNOWN;
        dev->vendor_id = p->vendor_id;
        dev->product_id = p->device_id;
        dev->class_id = p->class_code;
        dev->subclass_id = p->subclass;
        strcpy(dev->name, pci_class_name(p->class_code, p->subclass));
        dev->mounted = false;
        dev->removable = false;

        // Classify device type
        switch (p->class_code) {
            case 0x01:  // Storage
                dev->type = DEVICE_STORAGE;
                dev->bus = (p->subclass == 0x06) ? BUS_SATA : BUS_IDE;
                break;
            case 0x02:  // Network
                dev->type = DEVICE_NETWORK;
                break;
            case 0x03:  // Display
                dev->type = DEVICE_DISPLAY;
                break;
            case 0x0C:  // Serial bus (USB)
                if (p->subclass == 0x03) {
                    dev->type = DEVICE_USB;
                    dev->bus = BUS_USB;
                }
                break;
            default:
                break;
        }
    }
}

// Get device by index
DeviceInfo* get_device(int idx) {
    if (idx < 0 || idx >= g_device_count) return 0;
    return &g_devices[idx];
}

// Find device by type
DeviceInfo* find_device(DeviceType type) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].type == type) return &g_devices[i];
    }
    return 0;
}

// Get CPU info (via CPUID)
void get_cpu_info(CPUInfo& info) {
    memset(&info, 0, sizeof(info));
    // TODO: implement CPUID
    strcpy(info.model_name, "Unknown CPU");
    info.clock_mhz = 0;
}

// Get memory info
void get_memory_info(MemoryInfo& info) {
    memset(&info, 0, sizeof(info));
    // TODO: get from E820 or multiboot
    info.total_ram = 128 * 1024 * 1024;  // 128MB default
    info.total_pages = info.total_ram / 4096;
}

// Initialize hardware detection
void init() {
    detect_all();
}

} // namespace hw
} // namespace nefu
