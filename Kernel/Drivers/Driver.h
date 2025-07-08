#ifndef DRIVER_H
#define DRIVER_H

#include "../Core/stdint.h"

// Driver types
typedef enum {
    DRIVER_KEYBOARD,
    DRIVER_MOUSE,
    DRIVER_NETWORK,
    DRIVER_STORAGE,
    DRIVER_MAX
} DriverType;

// Driver interface - all drivers implement this
typedef struct {
    DriverType type;
    const char* name;
    void (*init)(void);
    void (*handle_interrupt)(uint8_t irq);
    int (*read)(void* buffer, uint32_t size);
    int (*write)(const void* buffer, uint32_t size);
} Driver;

// Driver registry
void DriverRegister(Driver* driver);
void DriverInit(void);
Driver* DriverGet(DriverType type);

#endif