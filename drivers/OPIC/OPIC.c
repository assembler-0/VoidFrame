/**
 * @file OPIC.c
 * @brief Driver for the AMD/Cyrix Open Programmable Interrupt Controller (OpenPIC).
 *
 * @warning This is a driver for deprecated legacy hardware from the mid-1990s.
 * The OpenPIC architecture was not widely adopted in x86 systems and was superseded
 * by the APIC architecture. This implementation is based on the OpenPIC v1.2
 * specification and is intended for compatibility with specific embedded hardware
 * like the AMD ÉlanSC520. Do not use this on modern systems.
 */

#include "OPIC.h"
#include "../../include/Io.h"
#include "../../kernel/core/Kernel.h"
#include "../../kernel/etc/Console.h"
#include "../../mm/VMem.h"
#include "x64.h"
#include "Panic.h"

// --- Register Definitions ---

// Default physical base address for the OpenPIC MMIO region (as per AMD ÉlanSC520)
#define OPIC_DEFAULT_PHYS_ADDR 0xFFFEF000

// Register blocks offsets from the base address
#define OPIC_PROCESSOR_BASE    0x00000
#define OPIC_GLOBAL_BASE       0x01000
#define OPIC_INTERRUPT_SOURCE_BASE 0x10000 // This is a guess, the doc is not super clear

// Per-Processor registers (relative to OPIC_PROCESSOR_BASE)
#define OPIC_REG_EOI           0x00A0  // End of Interrupt

// Global registers (relative to OPIC_GLOBAL_BASE)
#define OPIC_REG_FRR0          0x0020  // Feature Reporting Register 0
#define OPIC_REG_GCR0          0x0080  // Global Configuration Register 0
#define OPIC_REG_VENDOR_ID     0x01A0  // Vendor Identification Register

// FRR0 bitfields
#define OPIC_FRR0_LAST_SOURCE_SHIFT 16
#define OPIC_FRR0_LAST_SOURCE_MASK  0x07FF

// GCR0 bitfields
#define OPIC_GCR0_RESET             0x80000000 // Self-clearing reset bit

// Interrupt Source register fields (relative to OPIC_INTERRUPT_SOURCE_BASE)
// Each source has two 32-bit registers, but the doc specifies a 16-byte stride.
#define OPIC_IVPR_OFFSET(n) (OPIC_INTERRUPT_SOURCE_BASE + (n) * 0x20)

// IVPR bitfields
#define OPIC_IVPR_MASK              0x80000000 // 1 = Masked, 0 = Enabled

// --- Global Variables ---
static volatile uint32_t* s_opic_base = NULL;

// --- Forward Declarations ---
static void opic_write(uint32_t reg, uint32_t value);
static uint32_t opic_read(uint32_t reg);
static bool opic_detect();

// --- MMIO Functions ---

/**
 * @brief Writes a 32-bit value to an OpenPIC register.
 * @param reg The register offset from the OpenPIC base.
 * @param value The value to write.
 */
static void opic_write(uint32_t reg, uint32_t value) {
    // OpenPIC registers have a 16-byte stride.
    s_opic_base[reg / 4] = value;
}

/**
 * @brief Reads a 32-bit value from an OpenPIC register.
 * @param reg The register offset from the OpenPIC base.
 * @return The value read from the register.
 */
static uint32_t opic_read(uint32_t reg) {
    return s_opic_base[reg / 4];
}

// --- Core OPIC Functions ---

/**
 * @brief Probes for the OpenPIC device by checking the vendor ID.
 * @return true if the device is detected, false otherwise.
 */
static bool opic_detect() {
    PrintKernel("OPIC: Probing for OpenPIC device...\n");

    // Temporarily map the OpenPIC's default physical address to probe it.
    volatile uint32_t* probe_base = (volatile uint32_t*)VMemAlloc(PAGE_SIZE);
    if (!probe_base) {
        PrintKernelError("OPIC: Failed to allocate virtual memory for probing.\n");
        return false;
    }

    if (VMemUnmap((uint64_t)probe_base, PAGE_SIZE) != VMEM_SUCCESS) {
        PrintKernelError("OPIC: Failed to unmap OpenPIC MMIO.\n");
        return false;
    }

    if (VMemMapMMIO((uint64_t)probe_base, OPIC_DEFAULT_PHYS_ADDR, PAGE_SIZE, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        PrintKernelError("OPIC: Failed to map MMIO for probing.\n");
        VMemFree((void*)probe_base, PAGE_SIZE);
        return false;
    }

    uint32_t vendor_id = 0; // Read would #PF since OPIC don't exist anymore

    // Unmap the probed memory region immediately.
    VMemUnmapMMIO((uint64_t)probe_base, PAGE_SIZE);
    VMemFree((void*)probe_base, PAGE_SIZE);

    if (vendor_id == 0 || vendor_id == 0xFFFFFFFF) {
        PrintKernel("OPIC: No OpenPIC device found at default address.\n");
        return false;
    }

    PrintKernelF("OPIC: Detected device with Vendor ID: 0x%x\n", vendor_id);
    return true;
}


bool OpicInstall() {
    PrintKernelWarning("OpenPIC: Initializing deprecated OpenPIC driver.\n");

    // First, detect if the hardware is even present.
    if (!opic_detect()) {
        return false; // Do not proceed if no device is found.
    }

    // Map the OpenPIC MMIO region into virtual memory
    s_opic_base = (volatile uint32_t*)VMemAlloc(PAGE_SIZE);
    if (!s_opic_base) {
        PrintKernelError("OPIC: Failed to allocate virtual memory for OpenPIC.\n");
        return false;
    }

    if (VMemUnmap((uint64_t)s_opic_base, PAGE_SIZE) != VMEM_SUCCESS) {
        PrintKernelError("OPIC: Failed to unmap OpenPIC MMIO.\n");
        return false;
    }

    if (VMemMapMMIO((uint64_t)s_opic_base, OPIC_DEFAULT_PHYS_ADDR, PAGE_SIZE, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        PrintKernelError("OPIC: Failed to map OpenPIC MMIO.\n");
        VMemFree((void*)s_opic_base, PAGE_SIZE);
        s_opic_base = NULL;
        return false;
    }

    PrintKernelF("OPIC: Mapped physical address 0x%llx to virtual address 0x%llx\n",
                 (unsigned long long)OPIC_DEFAULT_PHYS_ADDR,
                 (unsigned long long)(uintptr_t)s_opic_base);

    // Reset the OpenPIC
    PrintKernel("OPIC: Resetting controller...\n");
    opic_write(OPIC_GLOBAL_BASE + OPIC_REG_GCR0, OPIC_GCR0_RESET);

    // Poll until the reset bit clears, with a timeout
    int timeout = 100000;
    while ((opic_read(OPIC_GLOBAL_BASE + OPIC_REG_GCR0) & OPIC_GCR0_RESET) && --timeout) {
        // Wait
    }

    if (timeout == 0) {
        PrintKernelError("OPIC: Timed out waiting for reset.\n");
        // Cleanup and fail
        VMemUnmap((uint64_t)s_opic_base, PAGE_SIZE);
        VMemFree((void*)s_opic_base, PAGE_SIZE);
        s_opic_base = NULL;
        return false;
    }

    // Mask all interrupts
    OpicMaskAll();

    PrintKernelSuccess("OPIC: Successfully initialized OpenPIC controller.\n");
    return true;
}

void OpicMaskAll() {
    if (!s_opic_base) {
        return;
    }

    // Read the Feature Reporting Register to find out how many interrupt sources there are.
    uint32_t frr0 = opic_read(OPIC_GLOBAL_BASE + OPIC_REG_FRR0);
    uint32_t num_sources = ((frr0 >> OPIC_FRR0_LAST_SOURCE_SHIFT) & OPIC_FRR0_LAST_SOURCE_MASK) + 1;

    PrintKernelF("OPIC: Found %u interrupt sources. Masking all...\n", num_sources);

    // Iterate through all interrupt sources and mask them.
    for (uint32_t i = 0; i < num_sources; ++i) {
        uint32_t ivpr_offset = OPIC_IVPR_OFFSET(i);
        uint32_t ivpr = opic_read(ivpr_offset);
        opic_write(ivpr_offset, ivpr | OPIC_IVPR_MASK);
    }
}

void OpicSendEoi() {
    if (s_opic_base) {
        // Writing any value to the EOI register signals the end of an interrupt.
        opic_write(OPIC_PROCESSOR_BASE + OPIC_REG_EOI, 0);
    }
}
