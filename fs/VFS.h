#pragma once
#include "BlockDevice.h"
#include "FileSystem.h"
#include "stdbool.h"
#include "stdint.h"
struct BlockDevice;
struct FileSystemDriver;

// VFS Mount Points
#define VFS_MAX_MOUNTS 8

typedef struct {
    char mount_point[64];
    struct BlockDevice* device;
    struct FileSystemDriver* fs_driver;
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
int VfsMount(const char* path, BlockDevice* device, FileSystemDriver* fs_driver);
int VfsUmount(const char* path);
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

int VfsReadAt(const char* path, void* buffer, uint32_t offset, uint32_t count);
int VfsWriteAt(const char* path, const void* buffer, uint32_t offset, uint32_t count);
int VfsInsertAt(const char* path, const void* buffer, uint32_t offset, uint32_t count);
int VfsDeleteAt(const char* path, uint32_t offset, uint32_t count);
int VfsReplaceAt(const char* path, const void* buffer, uint32_t offset, uint32_t old_count, uint32_t new_count);
int VfsTruncate(const char* path, uint32_t new_size);
int VfsExtend(const char* path, uint32_t additional_size, uint8_t fill_byte);
int VfsSwapRegions(const char* path, uint32_t offset1, uint32_t offset2, uint32_t count);
int VfsFillRegion(const char* path, uint32_t offset, uint32_t count, uint8_t pattern);
int VfsCompareRegions(const char* path1, uint32_t offset1, const char* path2, uint32_t offset2, uint32_t count);
int VfsCopyRegion(const char* src_path, uint32_t src_offset, const char* dst_path, uint32_t dst_offset, uint32_t count);
int VfsMoveRegion(const char* path, uint32_t src_offset, uint32_t dst_offset, uint32_t count);
int VfsSearchBytes(const char* path, const void* pattern, uint32_t pattern_size, uint32_t start_offset);
int VfsReplaceBytes(const char* path, const void* old_pattern, uint32_t old_size, const void* new_pattern, uint32_t new_size);
int VfsChecksum(const char* path, uint32_t offset, uint32_t count);
int VfsReverse(const char* path, uint32_t offset, uint32_t count);
int VfsRotate(const char* path, uint32_t offset, uint32_t count, int positions);
int VfsTransform(const char* path, uint32_t offset, uint32_t count, uint8_t (*transform_func)(uint8_t));
int VfsAnalyze(const char* path, uint32_t* byte_counts, uint32_t* entropy);

// Search modes
#define VFS_SEARCH_FIRST 0
#define VFS_SEARCH_LAST  1
#define VFS_SEARCH_ALL   2

// Transform operations
#define VFS_TRANSFORM_UPPERCASE 0
#define VFS_TRANSFORM_LOWERCASE 1
#define VFS_TRANSFORM_INVERT    2
#define VFS_TRANSFORM_ROT13     3

// Internal
VfsMountStruct* VfsFindMount(const char* path);
void VfsListMount(void);
const char* VfsStripMount(const char* path, VfsMountStruct* mount);
int VfsUnmountAll();