#ifndef VOIDFRAME_ISA_H
#define VOIDFRAME_ISA_H
#include <stdint.h>

// ISA Bus I/O port ranges
#define ISA_IO_BASE         0x000
#define ISA_IO_END          0x3FF
#define ISA_DMA_BASE        0x000
#define ISA_IRQ_CONTROLLER  0x020
#define ISA_TIMER           0x040
#define ISA_KEYBOARD        0x060
#define ISA_RTC             0x070
#define ISA_DMA_PAGE        0x080
#define ISA_INTERRUPT2      0x0A0
#define ISA_DMA2            0x0C0
#define ISA_MATH_COPROC     0x0F0
#define ISA_IDE_PRIMARY     0x1F0
#define ISA_GAME_PORT       0x201
#define ISA_LPT2            0x278
#define ISA_SERIAL2         0x2F8
#define ISA_LPT1            0x378
#define ISA_SERIAL1         0x3F8

// Common ISA IRQ assignments
#define ISA_IRQ_TIMER       0
#define ISA_IRQ_KEYBOARD    1
#define ISA_IRQ_CASCADE     2  // Connects to second PIC
#define ISA_IRQ_SERIAL2     3
#define ISA_IRQ_SERIAL1     4
#define ISA_IRQ_LPT2        5
#define ISA_IRQ_FLOPPY      6
#define ISA_IRQ_LPT1        7
#define ISA_IRQ_RTC         8
#define ISA_IRQ_FREE9       9
#define ISA_IRQ_FREE10      10
#define ISA_IRQ_FREE11      11
#define ISA_IRQ_MOUSE       12
#define ISA_IRQ_MATH        13
#define ISA_IRQ_IDE_PRIMARY 14
#define ISA_IRQ_IDE_SECOND  15

// Common DMA channels
#define ISA_DMA_FLOPPY      2
#define ISA_DMA_LPT1        3
#define ISA_DMA_SB_8BIT     1
#define ISA_DMA_SB_16BIT    5

// ISA device types
typedef enum {
    ISA_DEVICE_UNKNOWN,
    ISA_DEVICE_SERIAL,
    ISA_DEVICE_PARALLEL,
    ISA_DEVICE_SOUND,
    ISA_DEVICE_NETWORK,
    ISA_DEVICE_IDE,
    ISA_DEVICE_FLOPPY,
    ISA_DEVICE_GAME_PORT
} IsaDeviceType;

// ISA device descriptor
typedef struct {
    uint16_t io_base;
    uint16_t io_size;
    uint8_t irq;
    uint8_t dma_channel;
    IsaDeviceType type;
    char name[32];
    int active;
} IsaDevice;

// ISA Bus controller state
typedef struct {
    IsaDevice devices[16];  // Max 16 ISA devices
    int device_count;
    uint32_t io_bitmap[ISA_IO_END / 32 + 1];  // Track allocated I/O ports
} IsaBus;

void IsaInitBus(void);
int IsaRegisterDevice(uint16_t io_base, uint16_t io_size, uint8_t irq,
                      uint8_t dma, IsaDeviceType type, const char* name);
void IsaPrintDevices(void);
IsaDevice* IsaFindDeviceByType(IsaDeviceType type);
IsaDevice* IsaGetDevice(int device_id);
void IsaAutoDetect(void);
void IsaUnregisterDevice(int device_id);

#endif // VOIDFRAME_ISA_H
