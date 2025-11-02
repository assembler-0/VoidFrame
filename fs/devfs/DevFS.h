#ifndef VOIDFRAME_DEVFS_H
#define VOIDFRAME_DEVFS_H

#include <BlockDevice.h>

// This is a virtual filesystem, so it doesn't have a block device.
// The mount function is just a placeholder to satisfy the FileSystemDriver struct.
int DevfsMount(struct BlockDevice* device, const char* mount_point);

int DevfsReadFile(const char* path, void* buffer, uint32_t max_size);
int DevfsWriteFile(const char* path, const void* buffer, uint32_t size);
int DevfsListDir(const char* path);
int DevfsIsDir(const char* path);

#endif //VOIDFRAME_DEVFS_H
