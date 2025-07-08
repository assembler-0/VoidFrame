#include "Driver.h"
#include "../Core/Panic.h"

#define MAX_DRIVERS 16

static Driver* drivers[MAX_DRIVERS];
static int driver_count = 0;

void DriverRegister(Driver* driver) {
    if (!driver || driver_count >= MAX_DRIVERS) {
        Panic("Driver registration failed");
    }
    
    drivers[driver_count++] = driver;
}

void DriverInit(void) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i]->init) {
            drivers[i]->init();
        }
    }
}

Driver* DriverGet(DriverType type) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i]->type == type) {
            return drivers[i];
        }
    }
    return 0;
}