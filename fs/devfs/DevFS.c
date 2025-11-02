#include <DevFS.h>
#include <CharDevice.h>
#include <kernel/etc/Console.h>
#include <kernel/etc/StringOps.h>

int DevfsMount(struct BlockDevice* device, const char* mount_point) {
    // DevFS is a virtual filesystem and doesn't need a device.
    (void)device;
    (void)mount_point;
    return 0;
}

int DevfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    // The path is the device name, e.g. "/Serial"
    // We need to strip the leading '/'
    const char* dev_name = path + 1;
    CharDevice_t* dev = CharDeviceFind(dev_name);
    if (!dev || !dev->Read) {
        return -1;
    }
    return dev->Read(dev, buffer, max_size);
}

int DevfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    const char* dev_name = path + 1;
    CharDevice_t* dev = CharDeviceFind(dev_name);
    if (!dev || !dev->Write) {
        return -1;
    }
    return dev->Write(dev, buffer, size);
}

int DevfsListDir(const char* path) {
    // We only support listing the root of /Devices
    if (FastStrCmp(path, "/") != 0) {
        return -1;
    }

    int count = CharDeviceGetCount();
    for (int i = 0; i < count; i++) {
        CharDevice_t* dev = CharDeviceGet(i);
        if (dev) {
            PrintKernel(dev->name);
            PrintKernel("\n");
        }
    }
    return 0;
}

int DevfsIsDir(const char* path) {
    // Only the root of DevFS is considered a directory
    if (FastStrCmp(path, "/") == 0) {
        return 1;
    }
    return 0;
}
