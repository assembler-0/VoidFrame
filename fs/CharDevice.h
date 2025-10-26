#ifndef VOIDFRAME_CHARDEVICE_H
#define VOIDFRAME_CHARDEVICE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_CHAR_DEVICES 32

struct CharDevice;

typedef int (*CharReadFunc)(struct CharDevice* dev, void* buffer, uint32_t size);
typedef int (*CharWriteFunc)(struct CharDevice* dev, const void* buffer, uint32_t size);

typedef struct CharDevice {
    char name[32];
    CharReadFunc Read;
    CharWriteFunc Write;
} CharDevice_t;

void CharDeviceInit(void);
int CharDeviceRegister(CharDevice_t* device);
CharDevice_t* CharDeviceFind(const char* name);
CharDevice_t* CharDeviceGet(int index);
int CharDeviceGetCount(void);

#endif //VOIDFRAME_CHARDEVICE_H
