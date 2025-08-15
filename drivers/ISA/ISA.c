#include "ISA.h"
#include "Console.h"
#include "Io.h"
#include "KernelHeap.h"
#include "Pic.h"
#include "SB16.h"
#include "VMem.h"
#include "stdint.h"

static IsaBus g_isa_bus = {0};

// Check if I/O port range is available
static int IsaCheckPortRange(uint16_t base, uint16_t size) {
    for (uint16_t port = base; port < base + size; port++) {
        uint32_t word_index = port / 32;
        uint32_t bit_index = port % 32;
        if (g_isa_bus.io_bitmap[word_index] & (1U << bit_index)) {
            return 0;  // Port already allocated
        }
    }
    return 1;  // Range is free
}

// Allocate I/O port range
static void IsaAllocatePortRange(uint16_t base, uint16_t size) {
    for (uint16_t port = base; port < base + size; port++) {
        uint32_t word_index = port / 32;

        uint32_t bit_index = port % 32;
        g_isa_bus.io_bitmap[word_index] |= (1U << bit_index);
    }
}

// Free I/O port range
static void IsaFreePortRange(uint16_t base, uint16_t size) {
    for (uint16_t port = base; port < base + size; port++) {
        uint32_t word_index = port / 32;
        uint32_t bit_index = port % 32;
        g_isa_bus.io_bitmap[word_index] &= ~(1U << bit_index);
    }
}

// Probe for device at specific I/O address
static int IsaProbeDevice(uint16_t io_base, uint16_t io_size) {
    // Try to read from the device - if it returns 0xFF consistently,
    // there's probably nothing there (floating bus)
    uint8_t test_values[4];
    for (int i = 0; i < 4; i++) {
        test_values[i] = inb(io_base + i);
    }

    // If all reads return 0xFF, likely no device
    int all_ff = 1;
    for (int i = 0; i < 4; i++) {
        if (test_values[i] != 0xFF) {
            all_ff = 0;
            break;
        }
    }

    return !all_ff;  // Device present if not all 0xFF
}

// Initialize ISA bus
void IsaInitBus(void) {
    // Clear device list and I/O bitmap
    for (int i = 0; i < 16; i++) {
        g_isa_bus.devices[i].active = 0;
    }
    g_isa_bus.device_count = 0;

    for (int i = 0; i < (ISA_IO_END / 32 + 1); i++) {
        g_isa_bus.io_bitmap[i] = 0;
    }
}

// Register ISA device
// In ISA.c

// Register ISA device
int IsaRegisterDevice(uint16_t io_base, uint16_t io_size, uint8_t irq,
                     uint8_t dma, IsaDeviceType type, const char* name) {
    if (g_isa_bus.device_count >= 16) {
        PrintKernelError("ISA: Too many devices.\n");
        return -1;
    }

    // CRITICAL CHANGE: Check if I/O range is available BEFORE probing.
    // This prevents us from even touching a port range that is already
    // claimed by another driver (like xHCI).
    if (!IsaCheckPortRange(io_base, io_size)) {
        PrintKernelWarningF("ISA: I/O conflict for %s at 0x%X\n", name, io_base);
        return -2;
    }

    // The probe should now happen in IsaAutoDetect BEFORE calling this.
    // We assume if this function is called, the device is present.

    IsaDevice* dev = &g_isa_bus.devices[g_isa_bus.device_count];
    dev->io_base = io_base;
    dev->io_size = io_size;
    dev->irq = irq;
    dev->dma_channel = dma;
    dev->type = type;
    dev->active = 1;

    // Copy name safely
    for (int i = 0; i < 31 && name[i]; i++) {
        dev->name[i] = name[i];
    }
    dev->name[31] = '\0';

    // Allocate I/O ports ONLY after confirming they are free.
    IsaAllocatePortRange(io_base, io_size);
    PrintKernelF("ISA: Registered %s at I/O 0x%X\n", name, io_base);

    g_isa_bus.device_count++;
    return g_isa_bus.device_count - 1;
}

// Unregister ISA device
void IsaUnregisterDevice(int device_id) {
    if (device_id < 0 || device_id >= g_isa_bus.device_count) {
        return;
    }

    IsaDevice* dev = &g_isa_bus.devices[device_id];
    if (!dev->active) {
        return;
    }

    // Free I/O ports
    IsaFreePortRange(dev->io_base, dev->io_size);
    dev->active = 0;
}

// Auto-detect common ISA devices
void IsaAutoDetect(void) {
    PrintKernel("Starting ISA device auto-detection...\n");

    // Try to detect Serial ports
    if (IsaProbeDevice(ISA_SERIAL1, 8)) {
        IsaRegisterDevice(ISA_SERIAL1, 8, ISA_IRQ_SERIAL1, 0,
                         ISA_DEVICE_SERIAL, "COM1");
    }

    if (IsaProbeDevice(ISA_SERIAL2, 8)) {
        IsaRegisterDevice(ISA_SERIAL2, 8, ISA_IRQ_SERIAL2, 0,
                         ISA_DEVICE_SERIAL, "COM2");
    }

    // Try to detect Parallel ports
    if (IsaProbeDevice(ISA_LPT1, 3)) {
        IsaRegisterDevice(ISA_LPT1, 3, ISA_IRQ_LPT1, 0, // DMA for LPT is often 3, but can be unused.
                         ISA_DEVICE_PARALLEL, "LPT1");
    }

    // Try to detect IDE controller
    // This is a common one. A simple probe might be okay here.
    if (IsaProbeDevice(ISA_IDE_PRIMARY, 8)) {
        IsaRegisterDevice(ISA_IDE_PRIMARY, 8, ISA_IRQ_IDE_PRIMARY, 0,
                         ISA_DEVICE_IDE, "IDE Primary");
    }

    if (SB16_Probe(SB16_DSP_BASE)) {
        IsaRegisterDevice(SB16_DSP_BASE, 16, 5, ISA_DMA_SB_8BIT,
                         ISA_DEVICE_SOUND, "Sound Blaster 16");
        PrintKernel("Testing SB16 beep...\n");
        SB16_Beep(SB16_DSP_BASE);
    }

}
// Get device by index
IsaDevice* IsaGetDevice(int device_id) {
    if (device_id < 0 || device_id >= g_isa_bus.device_count) {
        return NULL;
    }

    IsaDevice* dev = &g_isa_bus.devices[device_id];
    return dev->active ? dev : NULL;
}

// Get device count
int IsaGetDeviceCount(void) {
    return g_isa_bus.device_count;
}

// Find device by type
IsaDevice* IsaFindDeviceByType(IsaDeviceType type) {
    for (int i = 0; i < g_isa_bus.device_count; i++) {
        IsaDevice* dev = &g_isa_bus.devices[i];
        if (dev->active && dev->type == type) {
            return dev;
        }
    }
    return NULL;
}

// Print all detected ISA devices (for debugging)
void IsaPrintDevices(void) {

    PrintKernelSuccess("ISA Bus Devices Found:\n");
    PrintKernelF("=====================\n");

    for (int i = 0; i < g_isa_bus.device_count; i++) {
        IsaDevice* dev = &g_isa_bus.devices[i];
        if (dev->active) {
            PrintKernelF("Device %d: %s\n", i, dev->name);
            PrintKernelF("  I/O Base: 0x%X, Size: %d bytes\n",
                        dev->io_base, dev->io_size);
            PrintKernelF("  IRQ: %d, DMA: %d\n", dev->irq, dev->dma_channel);
            PrintKernelF("\n");
        }
    }
}