#include "ISA.h"
#include "Console.h"
#include "Io.h"
#include "SB16.h"
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

    // Reserve system I/O ports that are always occupied
    // IsaAllocatePortRange(0x000, 0x020);  // DMA controller
    // IsaAllocatePortRange(0x020, 0x002);  // PIC 1
    // IsaAllocatePortRange(0x040, 0x004);  // Timer
    // IsaAllocatePortRange(0x060, 0x001);  // Keyboard controller
    // IsaAllocatePortRange(0x070, 0x002);  // RTC
    // IsaAllocatePortRange(0x0A0, 0x002);  // PIC 2
    // IsaAllocatePortRange(0x0C0, 0x020);  // DMA controller 2
    // IsaAllocatePortRange(0x0F0, 0x010);  // Math coprocessor
}

// Register ISA device
int IsaRegisterDevice(uint16_t io_base, uint16_t io_size, uint8_t irq,
                     uint8_t dma, IsaDeviceType type, const char* name) {
    if (g_isa_bus.device_count >= 16) {
        return -1;  // Too many devices
    }

    // Check if I/O range is available
    if (!IsaCheckPortRange(io_base, io_size)) {
        return -2;  // I/O conflict
    }

    // Probe for actual device presence
    if (!IsaProbeDevice(io_base, io_size)) {
        return -3;  // Device not detected
    }

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

    // Allocate I/O ports
    IsaAllocatePortRange(io_base, io_size);

    g_isa_bus.device_count++;
    return g_isa_bus.device_count - 1;  // Return device index
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
        IsaRegisterDevice(ISA_LPT1, 3, ISA_IRQ_LPT1, 0,
                         ISA_DEVICE_PARALLEL, "LPT1");
    }

    if (IsaProbeDevice(ISA_LPT2, 3)) {
        IsaRegisterDevice(ISA_LPT2, 3, ISA_IRQ_LPT2, 0,
                         ISA_DEVICE_PARALLEL, "LPT2");
    }

    // Try to detect Sound Blaster 16
    if (SB16_Probe(SB16_DSP_BASE)) {
        IsaRegisterDevice(SB16_DSP_BASE, 16, 5, ISA_DMA_SB_8BIT,
                         ISA_DEVICE_SOUND, "Sound Blaster 16");
    }

    // Try to detect Game Port
    if (IsaProbeDevice(ISA_GAME_PORT, 1)) {
        IsaRegisterDevice(ISA_GAME_PORT, 1, 0, 0,
                         ISA_DEVICE_GAME_PORT, "Game Port");
    }

    // Try to detect IDE controller
    if (IsaProbeDevice(ISA_IDE_PRIMARY, 8)) {
        IsaRegisterDevice(ISA_IDE_PRIMARY, 8, ISA_IRQ_IDE_PRIMARY, 0,
                         ISA_DEVICE_IDE, "IDE Primary");
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