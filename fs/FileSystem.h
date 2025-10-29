#pragma once

struct BlockDevice;

#define MAX_FILESYSTEM_DRIVERS 8

struct FileSystemDriver;

typedef int (*DetectFunc)(struct BlockDevice* device);
typedef int (*MountFunc)(struct BlockDevice* device, const char* mount_point);
typedef int (*UnmountFunc)(struct BlockDevice* device);

typedef struct FileSystemDriver {
    const char* name;
    DetectFunc detect;
    MountFunc mount;
    UnmountFunc unmount;
} FileSystemDriver;

void FileSystemInit();
int FileSystemRegister(struct FileSystemDriver* driver);
void FileSystemAutoMount();
