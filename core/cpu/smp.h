// nefuOS SMP (Symmetric Multi-Processing) Support
// - CPU detection / enumeration
// - AP (Application Processor) startup
// - Per-CPU data structure
// - Inter-processor interrupts (IPI)
// - CPU hotplug (simplified)

#pragma once

#include "../klib/klib.h"
#include <cstdint>

namespace nefu {
namespace smp {

// Per-CPU data structure
struct PerCPU {
    uint32_t cpu_id;            // APIC ID
    uint32_t lapic_id;          // Local APIC ID
    bool is_bsp;                // Bootstrap processor
    uint64_t kernel_stack;      // Per-CPU kernel stack
    uint64_t current_thread;    // Current running thread
    uint64_t idle_thread;       // Idle thread for this CPU
    uint32_t cpu_freq_mhz;      // CPU frequency
    bool online;                // CPU is online
};

// Max CPUs supported
const int MAX_CPUS = 32;

// Global CPU count
extern int g_cpu_count;
extern PerCPU g_percpu[MAX_CPUS];

// Local APIC registers (memory-mapped)
#define LAPIC_BASE      0xFEE00000  // Default LAPIC base

// LAPIC register offsets
#define LAPIC_ID        0x020
#define LAPIC_VERSION   0x030
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CURR 0x390
#define LAPIC_TIMER_DIV  0x3E0

// IPI delivery modes
#define IPI_FIXED      0x000
#define IPI_LOWEST     0x100
#define IPI_SMI        0x200
#define IPI_NMI        0x400
#define IPI_INIT       0x500
#define IPI_STARTUP    0x600

// IPI destination modes
#define IPI_DEST_PHYSICAL  0x00000
#define IPI_DEST_LOGICAL   0x80000

// IPI destination shorthand
#define IPI_DEST_SELF      0x40000
#define IPI_DEST_ALL_INCL  0x80000
#define IPI_DEST_ALL_EXCL  0xC0000

// LAPIC read/write
static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(LAPIC_BASE + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(LAPIC_BASE + reg) = val;
}

// Enable LAPIC
static inline void lapic_enable() {
    // Spurious Interrupt Vector Register: enable LAPIC
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | 0x100);
}

// Send EOI
static inline void lapic_eoi() {
    lapic_write(LAPIC_EOI, 0);
}

// Send IPI to a specific CPU
static inline void send_ipi(uint32_t lapic_id, uint32_t vector, uint32_t delivery_mode) {
    // Set destination (high bits)
    lapic_write(LAPIC_ICR_HIGH, lapic_id << 24);
    // Set vector, delivery mode, and trigger
    lapic_write(LAPIC_ICR_LOW, vector | delivery_mode | IPI_DEST_PHYSICAL);
}

// Send IPI to all CPUs (including self)
static inline void send_ipi_all(uint32_t vector, uint32_t delivery_mode) {
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, vector | delivery_mode | IPI_DEST_ALL_INCL);
}

// Send IPI to all CPUs (excluding self)
static inline void send_ipi_others(uint32_t vector, uint32_t delivery_mode) {
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, vector | delivery_mode | IPI_DEST_ALL_EXCL);
}

// IPI vectors
#define IPI_RESCHEDULE    0xF0    // Reschedule IPI
#define IPI_INVALIDATE_TLB 0xF1   // TLB shootdown
#define IPI_HALT          0xF2    // Halt IPI
#define IPI_PANIC         0xF3    // Panic IPI

// CPU detection (simplified - real implementation uses ACPI MADT)
void detect_cpus() {
    g_cpu_count = 1;
    g_percpu[0].cpu_id = 0;
    g_percpu[0].lapic_id = 0;
    g_percpu[0].is_bsp = true;
    g_percpu[0].online = true;
    g_percpu[0].cpu_freq_mhz = 0;  // TODO: detect via CPUID/APIC
}

// Get current CPU ID
int get_cpu_id() {
    // Read LAPIC ID
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

// Get current CPU's per-CPU data
PerCPU* get_current_cpu() {
    int id = get_cpu_id();
    if (id >= MAX_CPUS) id = 0;
    return &g_percpu[id];
}

// Send inter-processor interrupt
void ipi_send(int cpu, uint8_t vector) {
    if (cpu < 0 || cpu >= g_cpu_count) return;
    send_ipi(g_percpu[cpu].lapic_id, vector, IPI_FIXED);
}

// Broadcast IPI to all other CPUs
void ipi_broadcast(uint8_t vector) {
    send_ipi_others(vector, IPI_FIXED);
}

// CPU hotplug: bring up an AP
int cpu_online(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= MAX_CPUS) return -1;
    if (g_percpu[cpu_id].online) return 0;

    // Send INIT IPI
    send_ipi(g_percpu[cpu_id].lapic_id, 0, IPI_INIT);
    // Small delay
    for (volatile int i = 0; i < 1000000; i++);

    // Send STARTUP IPI twice
    send_ipi(g_percpu[cpu_id].lapic_id, 0, IPI_STARTUP);
    for (volatile int i = 0; i < 100000; i++);
    send_ipi(g_percpu[cpu_id].lapic_id, 0, IPI_STARTUP);

    g_percpu[cpu_id].online = true;
    return 0;
}

// CPU hotplug: take AP offline
int cpu_offline(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= g_cpu_count) return -1;
    if (g_percpu[cpu_id].is_bsp) return -1;  // Can't offline BSP

    send_ipi(g_percpu[cpu_id].lapic_id, 0, IPI_HALT);
    g_percpu[cpu_id].online = false;
    return 0;
}

// Initialize SMP
void init() {
    detect_cpus();
    lapic_enable();
}

} // namespace smp
} // namespace nefu
