#include "FileSystem.h"
#include "BlockDevice.h"
#include "Console.h"
#include "Format.h"
#include "VFS.h"
static FileSystemDriver* g_fs_drivers[MAX_FILESYSTEM_DRIVERS];
static int g_num_fs_drivers = 0;

void FileSystemInit() {
    g_num_fs_drivers = 0;
}

int FileSystemRegister(FileSystemDriver* driver) {
    if (g_num_fs_drivers >= MAX_FILESYSTEM_DRIVERS) {
        return -1; // No more space for new drivers
    }
    g_fs_drivers[g_num_fs_drivers++] = driver;
    PrintKernel("Filesystem driver registered: ");
    PrintKernel(driver->name);
    PrintKernel("\n");
    return 0;
}

void FileSystemAutoMount() {
    PrintKernel("FS: Starting filesystem auto-mount...\n");
    
    // Debug: Show all registered block devices
    PrintKernel("FS: Scanning for registered block devices...\n");
    int total_devices = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        BlockDevice* dev = BlockDeviceGet(i);
        if (dev && dev->active) {
            PrintKernel("FS: Found device ");
            PrintKernelInt(i);
            PrintKernel(": ");
            PrintKernel(dev->name);
            PrintKernel(" (active=");
            PrintKernelInt(dev->active);
            PrintKernel(", type=");
            PrintKernelInt(dev->type);
            PrintKernel(")\n");
            total_devices++;
        }
    }
    
    PrintKernel("FS: Total registered devices: ");
    PrintKernelInt(total_devices);
    PrintKernel("\n");
    
    if (total_devices == 0) {
        PrintKernelWarning("FS: No block devices registered! Check drive initialization.\n");
        return;
    }
    
    uint8_t mounted = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        BlockDevice* dev = BlockDeviceGet(i);
        if (dev && dev->active) {
            PrintKernel("FS: Checking device ");
            PrintKernel(dev->name);
            PrintKernel(" (type=");
            PrintKernelInt(dev->type);
            PrintKernel(")\n");
            
            for (int j = 0; j < g_num_fs_drivers; j++) {
                FileSystemDriver* driver = g_fs_drivers[j];
                PrintKernel("FS: Trying ");
                PrintKernel(driver->name);
                PrintKernel(" on ");
                PrintKernel(dev->name);
                PrintKernel("...\n");
                
                if (driver->detect(dev)) {
                    PrintKernel("FS: Detected ");
                    PrintKernel(driver->name);
                    PrintKernel(" on ");
                    PrintKernel(dev->name);
                    PrintKernel("\n");
                    // Simple mount point naming for now
                    char mount_point[64];
                    FormatA(mount_point, sizeof(mount_point), "%s/%s", RuntimeMounts, dev->name);
                    PrintKernel("FS: Mounting at ");
                    PrintKernel(mount_point);
                    PrintKernel("\n");
                    int mount_result = driver->mount(dev, mount_point);
                    if (mount_result == 0) {
                        PrintKernel("FS: Successfully mounted ");
                        PrintKernel(dev->name);
                        PrintKernel(" at ");
                        PrintKernel(mount_point);
                        PrintKernel("\n");
                        ++mounted;
                    } else {
                        PrintKernel("FS: Failed to mount ");
                        PrintKernel(dev->name);
                        PrintKernel(" (error: ");
                        PrintKernelInt(mount_result);
                        PrintKernel(")\n");
                    }
                    break; // Move to the next device once a filesystem is found
                } else {
                    PrintKernel("FS: No ");
                    PrintKernel(driver->name);
                    PrintKernel(" on ");
                    PrintKernel(dev->name);
                    PrintKernel("\n");
                }
            }
        }
    }
    if (!mounted) {
        PrintKernelWarning("FS: Automount: No filesystems detected\n");
    } else {
        PrintKernel("FS: Auto-mount complete, ");
        PrintKernelInt(mounted);
        PrintKernel(" filesystem(s) mounted\n");
    }
}

