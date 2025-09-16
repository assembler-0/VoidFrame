#include "Apic.h"
#include "../include/Io.h"
#include "../kernel/core/Kernel.h"
#include "../kernel/etc/Console.h"
#include "../mm/VMem.h"
#include "Cpu.h"
#include "sound/Generic.h" // For PIT definitions

// --- Register Definitions ---

// Local APIC registers (offsets from LAPIC base)
#define LAPIC_ID                        0x0020  // LAPIC ID
#define LAPIC_VER                       0x0030  // LAPIC Version
#define LAPIC_TPR                       0x0080  // Task Priority
#define LAPIC_EOI                       0x00B0  // EOI
#define LAPIC_LDR                       0x00D0  // Logical Destination
#define LAPIC_DFR                       0x00E0  // Destination Format
#define LAPIC_SVR                       0x00F0  // Spurious Interrupt Vector
#define LAPIC_ESR                       0x0280  // Error Status
#define LAPIC_ICR_LOW                   0x0300  // Interrupt Command Reg low
#define LAPIC_ICR_HIGH                  0x0310  // Interrupt Command Reg high
#define LAPIC_LVT_TIMER                 0x0320  // LVT Timer
#define LAPIC_LVT_LINT0                 0x0350  // LVT LINT0
#define LAPIC_LVT_LINT1                 0x0360  // LVT LINT1
#define LAPIC_LVT_ERROR                 0x0370  // LVT Error
#define LAPIC_TIMER_INIT_COUNT          0x0380  // Initial Count (for Timer)
#define LAPIC_TIMER_CUR_COUNT           0x0390  // Current Count (for Timer)
#define LAPIC_TIMER_DIV                 0x03E0  // Divide Configuration

// I/O APIC registers
#define IOAPIC_REG_ID                   0x00    // ID Register
#define IOAPIC_REG_VER                  0x01    // Version Register
#define IOAPIC_REG_TABLE                0x10    // Redirection Table

// --- Constants ---
#define APIC_BASE_MSR                   0x1B
#define APIC_BASE_MSR_ENABLE            0x800

#define IOAPIC_DEFAULT_PHYS_ADDR        0xFEC00000

// --- Global Variables ---
static volatile uint32_t* s_lapic_base = NULL;
static volatile uint32_t* s_ioapic_base = NULL;
static uint32_t s_apic_timer_freq_hz = 1000; // Default to 1KHz
volatile uint32_t s_apic_timer_ticks = 0;
volatile uint32_t APIC_HZ = 250;

// --- Forward Declarations ---
static void lapic_write(uint32_t reg, uint32_t value);
static uint32_t lapic_read(uint32_t reg);
static void ioapic_write(uint8_t reg, uint32_t value);
static uint32_t ioapic_read(uint8_t reg);
static void ioapic_set_entry(uint8_t index, uint64_t data);
static bool detect_apic();
static bool setup_lapic();
static bool setup_ioapic();

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

static uint16_t s_irq_mask = 0xFFFF; // All masked initially

// Helper to write the cached mask to the PICs
static void pic_write_mask() {
    outb(PIC1_DATA, s_irq_mask & 0xFF);
    outb(PIC2_DATA, (s_irq_mask >> 8) & 0xFF);
}

void PICMaskAll() {
    s_irq_mask = 0xFFFF;
    pic_write_mask();
}

// --- MMIO Functions ---

static void lapic_write(uint32_t reg, uint32_t value) {
    s_lapic_base[reg / 4] = value;
}

static uint32_t lapic_read(uint32_t reg) {
    return s_lapic_base[reg / 4];
}

static void ioapic_write(uint8_t reg, uint32_t value) {
    // I/O APIC uses an index/data pair for access
    s_ioapic_base[0] = reg;
    s_ioapic_base[4] = value;
}

static uint32_t ioapic_read(uint8_t reg) {
    s_ioapic_base[0] = reg;
    return s_ioapic_base[4];
}

// Sets a redirection table entry in the I/O APIC
static void ioapic_set_entry(uint8_t index, uint64_t data) {
    ioapic_write(IOAPIC_REG_TABLE + index * 2, (uint32_t)data);
    ioapic_write(IOAPIC_REG_TABLE + index * 2 + 1, (uint32_t)(data >> 32));
}

// --- Core APIC Functions ---

// Main entry point to initialize the APIC system
bool ApicInstall() {
    if (!detect_apic()) {
        PrintKernelError("APIC: No local APIC found or supported.\n");
        return false;
    }

    PICMaskAll();

    if (!setup_lapic()) {
        PrintKernelError("APIC: Failed to setup Local APIC.\n");
        return false;
    }

    if (!setup_ioapic()) {
        PrintKernelError("APIC: Failed to setup I/O APIC.\n");
        return false;
    }

    PrintKernelSuccess("APIC: Successfully initialized Local APIC and I/O APIC.\n");
    return true;
}

void ApicSendEoi() {
    lapic_write(LAPIC_EOI, 0);
}

// --- I/O APIC Interrupt Management ---

void ApicEnableIrq(uint8_t irq_line) {
    // IRQ line -> Vector 32 + IRQ
    // For now, a simple 1:1 mapping for legacy IRQs 0-15
    // This sends the interrupt to the bootstrap processor (LAPIC ID 0)
    uint64_t redirect_entry = (32 + irq_line); // Vector
    redirect_entry |= (0b000 << 8); // Delivery Mode: Fixed
    redirect_entry |= (0b0 << 11);  // Destination Mode: Physical
    redirect_entry |= (0b0 << 15);  // Polarity: High
    redirect_entry |= (0b0 << 13);  // Trigger Mode: Edge
    redirect_entry |= ((uint64_t)0 << 56); // Destination: LAPIC ID 0

    ioapic_set_entry(irq_line, redirect_entry);
}

void ApicDisableIrq(uint8_t irq_line) {
    // To disable, we set the mask bit (bit 16)
    uint64_t redirect_entry = (1 << 16);
    ioapic_set_entry(irq_line, redirect_entry);
}

void ApicMaskAll() {
    // Mask all 24 redirection entries in the I/O APIC
    for (int i = 0; i < 24; i++) {
        ApicDisableIrq(i);
    }
}

// --- APIC Timer Management ---

void ApicTimerInstall(uint32_t frequency_hz) {
    s_apic_timer_freq_hz = frequency_hz;

    // Set divide configuration to 16
    lapic_write(LAPIC_TIMER_DIV, 0x3);

    // Set the timer vector and mode (periodic)
    // Vector 32 is typically the timer interrupt
    lapic_write(LAPIC_LVT_TIMER, (32) | (0b001 << 17)); // Vector 32, Periodic mode

    // Calibrate and set the initial count
    ApicTimerSetFrequency(s_apic_timer_freq_hz);

    PrintKernelF("APIC: Timer installed at %d Hz.\n", frequency_hz);
}

void ApicTimerSetFrequency(uint32_t frequency_hz) {
    if (frequency_hz == 0) return;
    s_apic_timer_freq_hz = frequency_hz;
    APIC_HZ = frequency_hz;
    // To set the frequency, we need to know the APIC bus frequency.
    // A simple but effective way is to calibrate against the PIT.
    // 1. Tell LAPIC to count down from a large value.
    // 2. Use the PIT to wait for a known duration (e.g., 10ms).
    // 3. Read how much the LAPIC timer has decremented.
    // This gives us ticks per 10ms, from which we can calculate ticks per second.

    // For simplicity, this implementation will assume a fixed bus frequency.
    // A more robust solution requires PIT calibration.
    // Let's assume a 100MHz APIC bus clock for now.
    uint32_t apic_bus_freq = 100000000;
    uint32_t timer_divisor = 16;
    uint32_t ticks_per_second = apic_bus_freq / timer_divisor;
    uint32_t initial_count = ticks_per_second / frequency_hz;

    lapic_write(LAPIC_TIMER_INIT_COUNT, initial_count);
}


// --- Private Setup Functions ---

// Check for APIC presence via CPUID
static bool detect_apic() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (edx & (1 << 9)) != 0; // Check for APIC feature bit
}

// Initialize the Local APIC
static bool setup_lapic() {
    // Get LAPIC physical base address from MSR
    uint64_t lapic_base_msr = rdmsr(APIC_BASE_MSR);
    uint64_t lapic_phys_base = lapic_base_msr & 0xFFFFFF000;

    // Map the LAPIC into virtual memory
    s_lapic_base = (volatile uint32_t*)VMemAlloc(PAGE_SIZE);
    if (!s_lapic_base) {
        PrintKernelError("APIC: Failed to allocate virtual memory for LAPIC.\n");
        return false;
    }

    if (VMemUnmap((uint64_t)s_lapic_base, PAGE_SIZE) != VMEM_SUCCESS) {
        PrintKernelError("APIC: Failed to unmap LAPIC MMIO.\n");
        return false;
    }

    if (VMemMapMMIO((uint64_t)s_lapic_base, lapic_phys_base, PAGE_SIZE, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        PrintKernelError("APIC: Failed to map LAPIC MMIO.\n");
        return false;
    }

    // Enable the LAPIC by setting the enable bit in the MSR and the spurious vector register
    wrmsr(APIC_BASE_MSR, lapic_base_msr | APIC_BASE_MSR_ENABLE);

    // Set the spurious interrupt vector (0xFF) and enable the APIC software-wise
    lapic_write(LAPIC_SVR, 0x1FF);

    // Set TPR to 0 to accept all interrupts
    lapic_write(LAPIC_TPR, 0);

    PrintKernelF("APIC: LAPIC enabled at physical addr 0x%x, mapped to 0x%x\n", lapic_phys_base, (uint64_t)s_lapic_base);
    return true;
}

// Initialize the I/O APIC
static bool setup_ioapic() {
    // Map the I/O APIC into virtual memory. We assume the standard physical address.
    s_ioapic_base = (volatile uint32_t*)VMemAlloc(PAGE_SIZE);
    if (!s_ioapic_base) {
        PrintKernelError("APIC: Failed to allocate virtual memory for I/O APIC.\n");
        return false;
    }
    VMemUnmap((uint64_t)s_ioapic_base, PAGE_SIZE);
    if (VMemMapMMIO((uint64_t)s_ioapic_base, IOAPIC_DEFAULT_PHYS_ADDR, PAGE_SIZE, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        PrintKernelError("APIC: Failed to map I/O APIC MMIO.\n");
        return false;
    }

    // Read the I/O APIC version to verify it's working
    uint32_t version_reg = ioapic_read(IOAPIC_REG_VER);
    uint8_t max_redirects = (version_reg >> 16) & 0xFF;
    PrintKernelF("APIC: I/O APIC version %d, max redirects: %d\n", version_reg & 0xFF, max_redirects + 1);

    // Mask all interrupts initially
    ApicMaskAll();

    return true;
}