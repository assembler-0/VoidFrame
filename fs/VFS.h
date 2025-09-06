#pragma once
#include "stdbool.h"
#include "stdint.h"

// VFS Mount Points
#define VFS_MAX_MOUNTS 8

typedef enum {
    VFS_RAMFS = 0,
    VFS_FAT12 = 1,
    VFS_FAT16 = 2,
    VFS_EXT2 = 3
} VfsType;

typedef struct {
    char mount_point[64];
    VfsType type;
    uint8_t drive;  // For disk-based FS
    int active;
} VfsMountStruct;

static const char * SystemDir = "/System";
static const char * SystemKernel = "/System/Kernel";
static const char * SystemKernelLog = "/System/Kernel/sys.log";
static const char * SystemBoot = "/System/Boot";
static const char * SystemDrivers = "/System/Drivers";
static const char * SystemLibraries = "/System/Libraries";
static const char * SystemServices = "/System/Service";
static const char * SystemResources = "/System/Resources";

static const char * DataDir = "/Data";
static const char * DataApps = "/Data/Apps";
static const char * DataConfig = "/Data/Config";
static const char * DataCache = "/Data/Cache";
static const char * DataLogs = "/Data/Logs";
static const char * DataSpool = "/Data/Spool";
static const char * DataTemp = "/Data/Temp";

static const char * DevicesDir = "/Devices";
static const char * DevicesCpu = "/Devices/Cpu";
static const char * DevicesPci = "/Devices/Pci";
static const char * DevicesUsb = "/Devices/Usb";
static const char * DevicesStorage = "/Devices/Storage";
static const char * DevicesInput = "/Devices/Input";
static const char * DevicesGpu = "/Devices/Gpu";
static const char * DevicesNet = "/Devices/Net";
static const char * DevicesAcpi = "/Devices/Acpi";

static const char * UserDir = "/User";

static const char * RuntimeDir = "/Runtime";
static const char * RuntimeProcesses = "/Runtime/Processes";
static const char * RuntimeServices = "/Runtime/Services";
static const char * RuntimeIPC = "/Runtime/IPC";
static const char * RuntimeMounts = "/Runtime/Mounts";

// VFS Functions
int VfsInit(void);
int VfsMount(const char* path, VfsType type, uint8_t drive);
int VfsReadFile(const char* path, void* buffer, uint32_t max_size);
int VfsWriteFile(const char* path, const void* buffer, uint32_t size);
int VfsListDir(const char* path);
int VfsCreateFile(const char* path);
int VfsCreateDir(const char* path);
int VfsDelete(const char* path, bool Recursive);
int VfsIsDir(const char* path);
int VfsIsFile(const char* path);
uint64_t VfsGetFileSize(const char* path);
int VfsAppendFile(const char* path, const void* buffer, uint32_t size);
int VfsCopyFile(const char* src_path, const char* dest_path);
int VfsMoveFile(const char* src_path, const char* dest_path);
// Internal
VfsMountStruct* VfsFindMount(const char* path);
const char* VfsStripMount(const char* path, VfsMountStruct* mount);