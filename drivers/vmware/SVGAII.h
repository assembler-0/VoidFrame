#ifndef SVGAII_FIXED_H
#define SVGAII_FIXED_H

#include <stdint.h>
#include <stdbool.h>

// PCI IDs for VMware SVGA II
#define SVGAII_PCI_VENDOR_ID    0x15AD
#define SVGAII_PCI_DEVICE_ID    0x0405

// SVGA II Register Offsets
#define SVGA_INDEX              0x00
#define SVGA_VALUE              0x01
#define SVGA_BIOS               0x02
#define SVGA_IRQSTATUS          0x08

// SVGA II Register IDs - CORRECTED
#define SVGA_REG_ID             0
#define SVGA_REG_ENABLE         1
#define SVGA_REG_WIDTH          2
#define SVGA_REG_HEIGHT         3
#define SVGA_REG_MAX_WIDTH      4
#define SVGA_REG_MAX_HEIGHT     5
#define SVGA_REG_DEPTH          6
#define SVGA_REG_BPP            7
#define SVGA_REG_PSEUDOCOLOR    8
#define SVGA_REG_RED_MASK       9
#define SVGA_REG_GREEN_MASK     10
#define SVGA_REG_BLUE_MASK      11
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START       13
#define SVGA_REG_FB_OFFSET      14
#define SVGA_REG_VRAM_SIZE      15
#define SVGA_REG_FB_SIZE        16
#define SVGA_REG_CAPABILITIES   17
#define SVGA_REG_MEM_START      18
#define SVGA_REG_MEM_SIZE       19
#define SVGA_REG_CONFIG_DONE    20
#define SVGA_REG_SYNC           21
#define SVGA_REG_BUSY           22
#define SVGA_REG_GUEST_ID       23

// SVGA II ID values
#define SVGA_ID_0               0x90000000
#define SVGA_ID_1               0x90000001
#define SVGA_ID_2               0x90000002

// SVGA II Commands
#define SVGA_CMD_UPDATE         1
#define SVGA_CMD_RECT_COPY      3
#define SVGA_CMD_RECT_FILL      5

// SVGA II Capabilities
#define SVGA_CAP_RECT_FILL      0x00000001
#define SVGA_CAP_RECT_COPY      0x00000002
#define SVGA_CAP_ALPHA_CURSOR   0x00000008

// Device info structure
typedef struct {
    uint16_t io_port_base;
    uint32_t* framebuffer;
    uint32_t fb_size;
    uint32_t* fifo_ptr;
    uint32_t fifo_size;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    bool initialized;
} SVGAII_DeviceInfo;

extern SVGAII_DeviceInfo svgaII_device;

// Function prototypes
bool SVGAII_DetectAndInitialize(void);
void SVGAII_SetMode(uint32_t width, uint32_t height, uint32_t bpp);
void SVGAII_PutPixel(uint32_t x, uint32_t y, uint32_t color);
void SVGAII_UpdateScreen(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void SVGAII_FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

#endif // SVGAII_FIXED_H