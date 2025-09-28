#include "Ext2.h"

#include "../../kernel/atomic/Spinlock.h"
#include "../../kernel/etc/Console.h"
#include "../../kernel/etc/StringOps.h"
#include "../../kernel/sched/MLFQ.h"
#include "../../mm/KernelHeap.h"
#include "../../mm/MemOps.h"
#include "../VFS.h"
#include "FileSystem.h"
#include "Rtc.h"

#define EXT2_SUPERBLOCK_OFFSET 1024
#define EXT2_MAGIC 0xEF53

typedef struct {
    BlockDevice* device;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t num_groups;
    Ext2Superblock superblock;
    Ext2GroupDesc* group_descs;
    rwlock_t lock;
} Ext2Volume;

static Ext2Volume volume;

int Ext2Detect(BlockDevice* device) {
    PrintKernel("EXT2: Detecting EXT2 on device ");
    PrintKernel(device->name);
    PrintKernel("\n");
    
    uint8_t sb_buffer[1024];
    int read_result = BlockDeviceRead(device->id, 2, 2, sb_buffer);
    if (read_result != 0) {
        PrintKernel("EXT2: Failed to read superblock from device ");
        PrintKernel(device->name);
        PrintKernel(" (error: ");
        PrintKernelInt(read_result);
        PrintKernel(")\n");
        return 0;
    }

    Ext2Superblock* sb = (Ext2Superblock*)sb_buffer;
    PrintKernel("EXT2: Superblock magic = 0x");
    PrintKernelHex(sb->s_magic);
    PrintKernel(" (expected 0x");
    PrintKernelHex(EXT2_MAGIC);
    PrintKernel(")\n");
    
    if (sb->s_magic == EXT2_MAGIC) {
        PrintKernel("EXT2: Valid EXT2 filesystem detected on ");
        PrintKernel(device->name);
        PrintKernel("\n");
        return 1;
    }

    PrintKernel("EXT2: No EXT2 filesystem on ");
    PrintKernel(device->name);
    PrintKernel("\n");
    return 0;
}


static int Ext2WriteBlock(uint32_t block, const void* buffer) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    if (block >= volume.superblock.s_blocks_count) {
        PrintKernelF("EXT2: Block %u out of bounds (max: %u)",
                     block, volume.superblock.s_blocks_count - 1);
        WriteUnlock(&volume.lock);
        return -1;
    }
    uint32_t num_sectors   = volume.block_size / 512;
    if (BlockDeviceWrite(volume.device->id, block * num_sectors, num_sectors, buffer) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }
    WriteUnlock(&volume.lock);
    return 0;
}

// Helper to read a block from the disk
int Ext2ReadBlock(uint32_t block, void* buffer) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    if (block >= volume.superblock.s_blocks_count) {
        PrintKernelF("EXT2: Block %u out of bounds (max: %u)",
                     block, volume.superblock.s_blocks_count - 1);
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }
    uint32_t num_sectors   = volume.block_size / 512;
    if (BlockDeviceRead(volume.device->id, block * num_sectors, num_sectors, buffer) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return 0;
}

static FileSystemDriver ext2_driver = {"EXT2", Ext2Detect, Ext2Mount};

int Ext2Mount(BlockDevice* device, const char* mount_point) {
    volume.lock = (rwlock_t){0};
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    volume.device = device;

    uint8_t sb_buffer[1024];
    if (BlockDeviceRead(device->id, 2, 2, sb_buffer) != 0) {
        PrintKernelF("EXT2: Failed to read superblock.\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    FastMemcpy(&volume.superblock, sb_buffer, sizeof(Ext2Superblock));

    if (volume.superblock.s_magic != EXT2_MAGIC) {
        PrintKernelF("EXT2: Invalid magic number. Not an EXT2 filesystem.\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    if (volume.superblock.s_log_block_size > 10) {
        PrintKernelF("EXT2: Invalid block size shift: %u\n",
                     volume.superblock.s_log_block_size);
        WriteUnlock(&volume.lock);
        return -1;
    }
    volume.block_size = 1024 << volume.superblock.s_log_block_size;
    volume.inode_size = volume.superblock.s_inode_size;
    volume.blocks_per_group = volume.superblock.s_blocks_per_group;
    volume.inodes_per_group = volume.superblock.s_inodes_per_group;
    if (volume.blocks_per_group == 0) {
        PrintKernelF("EXT2: Invalid blocks_per_group: 0\n");
        WriteUnlock(&volume.lock);
        return -1;
    }
    volume.num_groups = (volume.superblock.s_blocks_count + volume.blocks_per_group - 1) / volume.blocks_per_group;

    PrintKernelF("EXT2: Block size: %d bytes\n", volume.block_size);
    PrintKernelF("EXT2: Inode size: %d bytes\n", volume.inode_size);
    PrintKernelF("EXT2: Block groups: %d\n", volume.num_groups);

    uint32_t bgdt_bytes = volume.num_groups * sizeof(Ext2GroupDesc);
    // Determine how many blocks are needed to store BGDT
    uint32_t bgdt_blocks = (bgdt_bytes + volume.block_size - 1) / volume.block_size;
    // Allocate buffer at block granularity
    uint32_t bgdt_alloc_size = bgdt_blocks * volume.block_size;
    uint8_t* bgdt_buffer = KernelMemoryAlloc(bgdt_alloc_size);
    if (!bgdt_buffer) {
        PrintKernelF("EXT2: Failed to allocate memory for BGD table.\n");
        WriteUnlock(&volume.lock);
        return -1;
    }
    // Read each BGDT block into buffer
    uint32_t bgdt_block = (volume.block_size == 1024) ? 2 : 1;
    for (uint32_t i = 0; i < bgdt_blocks; ++i) {
        if (Ext2ReadBlock(bgdt_block + i, bgdt_buffer + i * volume.block_size) != 0) {
            PrintKernelF("EXT2: Failed to read BGD table.\n");
            KernelFree(bgdt_buffer);
            WriteUnlock(&volume.lock);
            return -1;
        }
    }
    // Assign descriptor pointer to the allocated buffer
    volume.group_descs = (Ext2GroupDesc*)bgdt_buffer;

    PrintKernelF("EXT2: Mounting filesystem...\n");
    VfsCreateDir(mount_point);
    if (VfsMount(mount_point, device, &ext2_driver) != 0) {
        PrintKernelF("EXT2: Failed to register mount point %s\n", mount_point);
        KernelFree(volume.group_descs);
        volume.group_descs = NULL;
        volume.device = NULL;
        WriteUnlock(&volume.lock);
        return -1;
    }
    PrintKernelF("EXT2: Mounted filesystem\n");

    PrintKernelSuccess("EXT2: Filesystem initialized successfully.\n");
    WriteUnlock(&volume.lock);
    return 0;
}

int Ext2ReadInode(uint32_t inode_num, Ext2Inode* inode) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    uint32_t group = (inode_num - 1) / volume.inodes_per_group;
    if (group >= volume.num_groups) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    uint32_t index = (inode_num - 1) % volume.inodes_per_group;
    uint32_t inode_table_block = volume.group_descs[group].bg_inode_table;

    uint32_t block_offset = (index * volume.inode_size) / volume.block_size;
    uint32_t offset_in_block = (index * volume.inode_size) % volume.block_size;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    if (Ext2ReadBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    FastMemcpy(inode, block_buffer + offset_in_block, sizeof(Ext2Inode));

    KernelFree(block_buffer);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return 0;
}

static int Ext2WriteInode(uint32_t inode_num, Ext2Inode* inode) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    if (inode_num == 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    uint32_t group = (inode_num - 1) / volume.inodes_per_group;
    if (group >= volume.num_groups) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    uint32_t index = (inode_num - 1) % volume.inodes_per_group;
    uint32_t inode_table_block = volume.group_descs[group].bg_inode_table;

    uint32_t block_offset = (index * volume.inode_size) / volume.block_size;
    uint32_t offset_in_block = (index * volume.inode_size) % volume.block_size;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Read-modify-write the block containing the inode
    if (Ext2ReadBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        WriteUnlock(&volume.lock);
        return -1;
    }

    FastMemcpy(block_buffer + offset_in_block, inode, sizeof(Ext2Inode));

    if (Ext2WriteBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        WriteUnlock(&volume.lock);
        return -1;
    }

    KernelFree(block_buffer);
    WriteUnlock(&volume.lock);
    return 0;
}

// Find a directory entry in a directory inode
uint32_t Ext2FindInDir(Ext2Inode* dir_inode, const char* name) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    if (!S_ISDIR(dir_inode->i_mode)) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0; // Not a directory
    }

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    for (int i = 0; i < 12; i++) { // Only direct blocks
        if (dir_inode->i_block[i] == 0) continue;

        if (Ext2ReadBlock(dir_inode->i_block[i], block_buffer) != 0) {
            continue;
        }

        Ext2DirEntry* entry = (Ext2DirEntry*)block_buffer;
        uint32_t offset = 0;
        while (offset < volume.block_size && entry->rec_len > 0) {
            if (entry->inode != 0 && entry->name_len == FastStrlen(name, 255)) {
                if (FastMemcmp(entry->name, name, entry->name_len) == 0) {
                    uint32_t inode_num = entry->inode;
                    KernelFree(block_buffer);
                    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
                    return inode_num;
                }
            }
            offset += entry->rec_len;
            entry = (Ext2DirEntry*)((uint8_t*)block_buffer + offset);
        }
    }

    KernelFree(block_buffer);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return 0; // Not found
}

uint32_t Ext2PathToInode(const char* path) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    

    if (path[0] == '/' && (path[1] == '\0' || path[1] == ' ')) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 2; // Root directory inode
    }

    // Start from root inode
    uint32_t current_inode_num = 2;
    Ext2Inode current_inode;
    if (Ext2ReadInode(current_inode_num, &current_inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    char component[256];
    const char* p = path;
    if (*p == '/') p++;

    while (*p) {
        // Extract next path component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';

        if (!S_ISDIR(current_inode.i_mode)) {
            ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
            return 0; // Not a directory, but path continues
        }

        current_inode_num = Ext2FindInDir(&current_inode, component);
        if (current_inode_num == 0) {
            ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
            return 0; // Component not found
        }

        if (Ext2ReadInode(current_inode_num, &current_inode) != 0) {
            ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
            return 0; // Failed to read next inode
        }

        if (*p == '/') p++;
    }

    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return current_inode_num;
}

int Ext2ReadFile(const char* path, void* buffer, uint32_t max_size) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1; // Not found
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1; // Failed to read inode
    }

    if (!S_ISREG(inode.i_mode)) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1; // Not a regular file
    }

    uint32_t file_size = inode.i_size;
    uint32_t bytes_to_read = (file_size < max_size) ? file_size : max_size;
    uint32_t bytes_read = 0;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    // Only handle direct blocks for now
    for (int i = 0; i < 12 && bytes_read < bytes_to_read; i++) {
        if (inode.i_block[i] == 0) continue;

        if (Ext2ReadBlock(inode.i_block[i], block_buffer) != 0) {
            KernelFree(block_buffer);
            ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
            return -1;
        }

        uint32_t remaining_in_block = volume.block_size;
        uint32_t remaining_in_file = bytes_to_read - bytes_read;
        uint32_t copy_size = (remaining_in_block < remaining_in_file) ? remaining_in_block : remaining_in_file;

        FastMemcpy((uint8_t*)buffer + bytes_read, block_buffer, copy_size);
        bytes_read += copy_size;
    }

    KernelFree(block_buffer);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return bytes_read;
}

int Ext2WriteFile(const char* path, const void* buffer, uint32_t size) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    uint32_t inode_num = Ext2PathToInode(path);

    // If file doesn't exist, create it automatically
    if (inode_num == 0) {
        if (Ext2CreateFile(path) != 0) {
            PrintKernelF("EXT2: WriteFile: Failed to create file: %s\n", path);
            WriteUnlock(&volume.lock);
            return -1;
        }
        inode_num = Ext2PathToInode(path);
        if (inode_num == 0) {
            PrintKernelF("EXT2: WriteFile: Failed to find created file: %s\n", path);
            WriteUnlock(&volume.lock);
            return -1;
        }
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    if (!S_ISREG(inode.i_mode)) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    int bytes_written = 0;  // Changed from uint32_t to int
    const uint8_t* data_buffer = (const uint8_t*)buffer;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Write to direct blocks, overwriting existing data
    for (int i = 0; i < 12; i++) {
        if (bytes_written >= (int)size) break;
        if (inode.i_block[i] == 0) {
            PrintKernelF("EXT2: WriteFile: Reached end of allocated blocks for %s. File extension not yet supported.\n", path);
            break;
        }

        uint32_t remaining_to_write = size - bytes_written;
        uint32_t chunk_size = (remaining_to_write > volume.block_size) ? volume.block_size : remaining_to_write;

        if (chunk_size < volume.block_size) {
            // Partial block: Read-Modify-Write
            if (Ext2ReadBlock(inode.i_block[i], block_buffer) != 0) {
                bytes_written = -1;
                break;
            }
            FastMemcpy(block_buffer, data_buffer + bytes_written, chunk_size);
            if (Ext2WriteBlock(inode.i_block[i], block_buffer) != 0) {
                bytes_written = -1;
                break;
            }
        } else {
            // Full block write
            if (Ext2WriteBlock(inode.i_block[i], data_buffer + bytes_written) != 0) {
                bytes_written = -1;
                break;
            }
        }
        bytes_written += chunk_size;
    }

    KernelFree(block_buffer);

    if (bytes_written > 0) {
        if ((uint32_t)bytes_written != inode.i_size) {
            inode.i_size = bytes_written;
            if (Ext2WriteInode(inode_num, &inode) != 0) {
                WriteUnlock(&volume.lock);
                return -1;
            }
        }
    }

    if (bytes_written <= 0 && size > 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    WriteUnlock(&volume.lock);
    return bytes_written;
}


int Ext2ListDir(const char* path) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    if (!S_ISDIR(inode.i_mode)) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return -1;
    }

    PrintKernelF("Listing directory: %s\n", path);

    // Direct blocks only
    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] == 0) continue;
        if (Ext2ReadBlock(inode.i_block[i], block_buffer) != 0) continue;

        Ext2DirEntry* entry = (Ext2DirEntry*)block_buffer;
        uint32_t offset = 0;
        while (offset < volume.block_size) {
            if (entry->inode == 0) break;

            char name_buf[256];
            FastMemcpy(name_buf, entry->name, entry->name_len);
            name_buf[entry->name_len] = '\0';
            PrintKernelF("  %s\n", name_buf);

            offset += entry->rec_len;
            entry = (Ext2DirEntry*)((uint8_t*)entry + entry->rec_len);
        }
    }

    KernelFree(block_buffer);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return 0;
}


static int Ext2FindFreeBit(uint8_t* bitmap, uint32_t size_in_bits) {
    for (uint32_t i = 0; i < size_in_bits; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        if (!(bitmap[byte_idx] & (1 << bit_idx))) {
            return i;
        }
    }
    return -1;
}

static void Ext2SetBit(uint8_t* bitmap, uint32_t bit) {
    uint32_t byte_idx = bit / 8;
    uint32_t bit_idx = bit % 8;
    bitmap[byte_idx] |= (1 << bit_idx);
}

static void Ext2ClearBit(uint8_t* bitmap, uint32_t bit) {
    uint32_t byte_idx = bit / 8;
    uint32_t bit_idx = bit % 8;
    bitmap[byte_idx] &= ~(1 << bit_idx);
}

static uint32_t Ext2AllocateInode() {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    uint8_t* bitmap_buffer = KernelMemoryAlloc(volume.block_size);
    if (!bitmap_buffer) {
        WriteUnlock(&volume.lock);
        return 0;
    }

    for (uint32_t group = 0; group < volume.num_groups; group++) {
        uint32_t inode_bitmap_block = volume.group_descs[group].bg_inode_bitmap;

        if (Ext2ReadBlock(inode_bitmap_block, bitmap_buffer) != 0) {
            continue;
        }

        int free_bit = Ext2FindFreeBit(bitmap_buffer, volume.inodes_per_group);
        if (free_bit != -1) {
            Ext2SetBit(bitmap_buffer, free_bit);
            if (Ext2WriteBlock(inode_bitmap_block, bitmap_buffer) == 0) {
                volume.group_descs[group].bg_free_inodes_count--;
                uint32_t bgdt_block = (volume.block_size == 1024) ? 2 : 1;
                Ext2WriteBlock(bgdt_block, volume.group_descs);

                volume.superblock.s_free_inodes_count--;

                KernelFree(bitmap_buffer);
                WriteUnlock(&volume.lock);
                return group * volume.inodes_per_group + free_bit + 1;
            }
        }
    }

    KernelFree(bitmap_buffer);
    WriteUnlock(&volume.lock);
    return 0;
}

static uint32_t Ext2AllocateBlock() {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    uint8_t* bitmap_buffer = KernelMemoryAlloc(volume.block_size);
    if (!bitmap_buffer) {
        WriteUnlock(&volume.lock);
        return 0;
    }

    for (uint32_t group = 0; group < volume.num_groups; group++) {
        uint32_t block_bitmap_block = volume.group_descs[group].bg_block_bitmap;

        if (Ext2ReadBlock(block_bitmap_block, bitmap_buffer) != 0) {
            continue;
        }

        int free_bit = Ext2FindFreeBit(bitmap_buffer, volume.blocks_per_group);
        if (free_bit != -1) {
            Ext2SetBit(bitmap_buffer, free_bit);
            if (Ext2WriteBlock(block_bitmap_block, bitmap_buffer) == 0) {
                volume.group_descs[group].bg_free_blocks_count--;
                uint32_t bgdt_block = (volume.block_size == 1024) ? 2 : 1;
                Ext2WriteBlock(bgdt_block, volume.group_descs);

                volume.superblock.s_free_blocks_count--;

                KernelFree(bitmap_buffer);
                WriteUnlock(&volume.lock);
                return group * volume.blocks_per_group + free_bit + volume.superblock.s_first_data_block;
            }
        }
    }

    KernelFree(bitmap_buffer);
    WriteUnlock(&volume.lock);
    return 0;
}

static int Ext2AddDirEntry(uint32_t dir_inode_num, const char* name, uint32_t file_inode_num, uint8_t file_type) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    Ext2Inode dir_inode;
    if (Ext2ReadInode(dir_inode_num, &dir_inode) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    if (!S_ISDIR(dir_inode.i_mode)) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    uint16_t name_len = FastStrlen(name, 255);
    uint16_t required_len = 8 + name_len;
    if (required_len % 4 != 0) required_len += 4 - (required_len % 4);

    // Look for space in existing blocks
    for (int i = 0; i < 12; i++) {
        if (dir_inode.i_block[i] == 0) continue;

        if (Ext2ReadBlock(dir_inode.i_block[i], block_buffer) != 0) continue;

        Ext2DirEntry* entry = (Ext2DirEntry*)block_buffer;
        uint32_t offset = 0;
        Ext2DirEntry* last_entry = NULL;

        while (offset < volume.block_size && entry->rec_len > 0) {
            last_entry = entry;
            offset += entry->rec_len;
            if (offset >= volume.block_size) break;
            entry = (Ext2DirEntry*)((uint8_t*)block_buffer + offset);
        }

        if (last_entry && offset <= volume.block_size) {
            uint16_t actual_len = 8 + last_entry->name_len;
            if (actual_len % 4 != 0) actual_len += 4 - (actual_len % 4);

            uint16_t available_space = last_entry->rec_len - actual_len;

            if (available_space >= required_len) {
                last_entry->rec_len = actual_len;

                Ext2DirEntry* new_entry = (Ext2DirEntry*)((uint8_t*)last_entry + actual_len);
                new_entry->inode = file_inode_num;
                new_entry->rec_len = available_space;
                new_entry->name_len = name_len;
                new_entry->file_type = file_type;
                FastMemcpy(new_entry->name, name, name_len);

                if (Ext2WriteBlock(dir_inode.i_block[i], block_buffer) == 0) {
                    KernelFree(block_buffer);
                    WriteUnlock(&volume.lock);
                    return 0;
                }
            }
        }
    }

    // Need to allocate a new block for the directory
    for (int i = 0; i < 12; i++) {
        if (dir_inode.i_block[i] == 0) {
            uint32_t new_block = Ext2AllocateBlock();
            if (new_block == 0) break;

            dir_inode.i_block[i] = new_block;
            dir_inode.i_size += volume.block_size;

            FastMemset(block_buffer, 0, volume.block_size);
            Ext2DirEntry* entry = (Ext2DirEntry*)block_buffer;
            entry->inode = file_inode_num;
            entry->rec_len = volume.block_size;
            entry->name_len = name_len;
            entry->file_type = file_type;
            FastMemcpy(entry->name, name, name_len);

            if (Ext2WriteBlock(new_block, block_buffer) == 0 &&
                Ext2WriteInode(dir_inode_num, &dir_inode) == 0) {
                KernelFree(block_buffer);
                WriteUnlock(&volume.lock);
                return 0;
            }
            break;
        }
    }

    KernelFree(block_buffer);
    WriteUnlock(&volume.lock);
    return -1;
}

int Ext2CreateFile(const char* path) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    // Extract directory path and filename
    char dir_path[256] = "/";
    char filename[256];

    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash && last_slash != path) {
        int dir_len = last_slash - path;
        if (dir_len >= 255) {
            WriteUnlock(&volume.lock);
            return -1;
        }
        FastMemcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        FastStrCopy(filename, last_slash + 1, 255);
    } else {
        FastStrCopy(filename, last_slash ? last_slash + 1 : path, 255);
    }

    // Check if file already exists
    if (Ext2PathToInode(path) != 0) {
        WriteUnlock(&volume.lock);
        return 0; // Success - file exists
    }

    // Find parent directory
    uint32_t parent_inode_num = Ext2PathToInode(dir_path);
    if (parent_inode_num == 0) {
        PrintKernelF("EXT2: CreateFile: Parent directory not found: %s\n", dir_path);
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Allocate new inode
    uint32_t new_inode_num = Ext2AllocateInode();
    if (new_inode_num == 0) {
        PrintKernelF("EXT2: CreateFile: Failed to allocate inode\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Allocate first data block
    uint32_t first_block = Ext2AllocateBlock();
    if (first_block == 0) {
        PrintKernelF("EXT2: CreateFile: Failed to allocate data block\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Initialize inode
    Ext2Inode new_inode;
    FastMemset(&new_inode, 0, sizeof(Ext2Inode));
    new_inode.i_mode = 0x8000 | 0x1FF; // Regular file with 777 permissions
    new_inode.i_uid = 0;
    new_inode.i_size = 0;
    new_inode.i_gid = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = volume.block_size / 512;
    new_inode.i_block[0] = first_block;
    for (int i = 1; i < 15; i++) {
        new_inode.i_block[i] = 0;
    }

    // Write inode
    if (Ext2WriteInode(new_inode_num, &new_inode) != 0) {
        PrintKernelF("EXT2: CreateFile: Failed to write inode\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Add directory entry
    if (Ext2AddDirEntry(parent_inode_num, filename, new_inode_num, 1) != 0) { // 1 = regular file
        PrintKernelF("EXT2: CreateFile: Failed to add directory entry\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Initialize the data block with zeros
    uint8_t* zero_buffer = KernelMemoryAlloc(volume.block_size);
    if (zero_buffer) {
        FastMemset(zero_buffer, 0, volume.block_size);
        Ext2WriteBlock(first_block, zero_buffer);
        KernelFree(zero_buffer);
    }

    PrintKernelSuccessF("EXT2: Created file: %s (inode %u)\n", path, new_inode_num);
    WriteUnlock(&volume.lock);
    return 0;
}



int Ext2CreateDir(const char* path) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    // Extract directory path and dirname
    char parent_path[256] = "/";
    char dirname[256];

    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash && last_slash != path) {
        int parent_len = last_slash - path;
        if (parent_len >= 255) {
            WriteUnlock(&volume.lock);
            return -1;
        }
        FastMemcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
        FastStrCopy(dirname, last_slash + 1, 255);
    } else {
        FastStrCopy(dirname, last_slash ? last_slash + 1 : path, 255);
    }

    // Check if directory already exists
    if (Ext2PathToInode(path) != 0) {
        WriteUnlock(&volume.lock);
        return 0;
    }

    // Find parent directory
    uint32_t parent_inode_num = Ext2PathToInode(parent_path);
    if (parent_inode_num == 0) {
        PrintKernelF("EXT2: CreateDir: Parent directory not found: %s\n", parent_path);
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Allocate new inode
    uint32_t new_inode_num = Ext2AllocateInode();
    if (new_inode_num == 0) {
        PrintKernelF("EXT2: CreateDir: Failed to allocate inode\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Allocate data block for directory entries
    uint32_t dir_block = Ext2AllocateBlock();
    if (dir_block == 0) {
        PrintKernelF("EXT2: CreateDir: Failed to allocate data block\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Initialize inode
    Ext2Inode new_inode;
    FastMemset(&new_inode, 0, sizeof(Ext2Inode));
    new_inode.i_mode = 0x4000 | 0x1FF; // Directory with 777 permissions
    new_inode.i_uid = 0;
    new_inode.i_size = volume.block_size;
    new_inode.i_gid = 0;
    new_inode.i_links_count = 2; // . and parent link
    new_inode.i_blocks = volume.block_size / 512;
    new_inode.i_block[0] = dir_block;
    for (int i = 1; i < 15; i++) {
        new_inode.i_block[i] = 0;
    }

    // Create directory entries (. and ..)
    uint8_t* dir_buffer = KernelMemoryAlloc(volume.block_size);
    if (!dir_buffer) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    FastMemset(dir_buffer, 0, volume.block_size);

    // . entry
    Ext2DirEntry* dot_entry = (Ext2DirEntry*)dir_buffer;
    dot_entry->inode = new_inode_num;
    dot_entry->rec_len = 12;
    dot_entry->name_len = 1;
    dot_entry->file_type = 2; // Directory
    dot_entry->name[0] = '.';

    // .. entry
    Ext2DirEntry* dotdot_entry = (Ext2DirEntry*)(dir_buffer + 12);
    dotdot_entry->inode = parent_inode_num;
    dotdot_entry->rec_len = volume.block_size - 12;
    dotdot_entry->name_len = 2;
    dotdot_entry->file_type = 2; // Directory
    dotdot_entry->name[0] = '.';
    dotdot_entry->name[1] = '.';

    // Write directory block and inode
    if (Ext2WriteBlock(dir_block, dir_buffer) != 0 ||
        Ext2WriteInode(new_inode_num, &new_inode) != 0) {
        KernelFree(dir_buffer);
        PrintKernelF("EXT2: CreateDir: Failed to write directory data\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    KernelFree(dir_buffer);

    // Add directory entry to parent
    if (Ext2AddDirEntry(parent_inode_num, dirname, new_inode_num, 2) != 0) { // 2 = directory
        PrintKernelF("EXT2: CreateDir: Failed to add directory entry\n");
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Update parent directory link count
    Ext2Inode parent_inode;
    if (Ext2ReadInode(parent_inode_num, &parent_inode) == 0) {
        parent_inode.i_links_count++;
        Ext2WriteInode(parent_inode_num, &parent_inode);
    }

    PrintKernelSuccessF("EXT2: Created directory: %s (inode %u)\n", path, new_inode_num);
    WriteUnlock(&volume.lock);
    return 0;
}


static void Ext2FreeBlock(uint32_t block_num) {
    if (block_num == 0) return;

    uint32_t group = (block_num - volume.superblock.s_first_data_block) / volume.blocks_per_group;
    uint32_t bit = (block_num - volume.superblock.s_first_data_block) % volume.blocks_per_group;

    uint8_t* bitmap_buffer = KernelMemoryAlloc(volume.block_size);
    if (!bitmap_buffer) return;

    uint32_t bitmap_block = volume.group_descs[group].bg_block_bitmap;
    if (Ext2ReadBlock(bitmap_block, bitmap_buffer) == 0) {
        Ext2ClearBit(bitmap_buffer, bit);
        if (Ext2WriteBlock(bitmap_block, bitmap_buffer) == 0) {
            volume.group_descs[group].bg_free_blocks_count++;
            volume.superblock.s_free_blocks_count++;
        }
    }

    KernelFree(bitmap_buffer);
}

static void Ext2FreeInode(uint32_t inode_num) {
    if (inode_num < 2) return;

    uint32_t group = (inode_num - 1) / volume.inodes_per_group;
    uint32_t bit = (inode_num - 1) % volume.inodes_per_group;

    uint8_t* bitmap_buffer = KernelMemoryAlloc(volume.block_size);
    if (!bitmap_buffer) return;

    uint32_t bitmap_block = volume.group_descs[group].bg_inode_bitmap;
    if (Ext2ReadBlock(bitmap_block, bitmap_buffer) == 0) {
        Ext2ClearBit(bitmap_buffer, bit);
        if (Ext2WriteBlock(bitmap_block, bitmap_buffer) == 0) {
            volume.group_descs[group].bg_free_inodes_count++;
            volume.superblock.s_free_inodes_count++;
        }
    }

    KernelFree(bitmap_buffer);
}


int Ext2Delete(const char* path) {
    WriteLock(&volume.lock, MLFQGetCurrentProcess()->pid);

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        PrintKernelF("EXT2: Delete: File not found: %s\n", path);
        WriteUnlock(&volume.lock);
        return -1; // Not found
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    // Free all data blocks
    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] != 0) {
            Ext2FreeBlock(inode.i_block[i]);
            inode.i_block[i] = 0;
        }
    }
    // TODO: Handle indirect blocks

    // Mark inode as deleted
    inode.i_dtime = RtcGetUnixTime();
    inode.i_links_count = 0;
    if (Ext2WriteInode(inode_num, &inode) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    Ext2FreeInode(inode_num);

    // Remove directory entry
    char dir_path[256] = "/";
    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (last_slash && last_slash != path) {
        int dir_len = last_slash - path;
        if (dir_len < 256) {
            FastMemcpy(dir_path, path, dir_len);
            dir_path[dir_len] = '\0';
        }
    }

    uint32_t parent_inode_num = Ext2PathToInode(dir_path);
    if (parent_inode_num == 0) {
        WriteUnlock(&volume.lock);
        return -1; // Should not happen
    }

    Ext2Inode parent_inode;
    if (Ext2ReadInode(parent_inode_num, &parent_inode) != 0) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) {
        WriteUnlock(&volume.lock);
        return -1;
    }

    int res = -1;
    for (int i = 0; i < 12; i++) {
        if (parent_inode.i_block[i] == 0) continue;
        if (Ext2ReadBlock(parent_inode.i_block[i], block_buffer) != 0) continue;

        Ext2DirEntry* entry = (Ext2DirEntry*)block_buffer;
        Ext2DirEntry* prev_entry = NULL;
        uint32_t offset = 0;

        while (offset < volume.block_size && entry->rec_len > 0) {
            if (entry->inode == inode_num) {
                if (prev_entry) {
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    entry->inode = 0; // First entry in block
                }
                if (Ext2WriteBlock(parent_inode.i_block[i], block_buffer) == 0) {
                    res = 0;
                }
                goto end_delete_loop;
            }
            prev_entry = entry;
            offset += entry->rec_len;
            entry = (Ext2DirEntry*)((uint8_t*)block_buffer + offset);
        }
    }

end_delete_loop:
    KernelFree(block_buffer);
    WriteUnlock(&volume.lock);
    return res;
}


int Ext2IsFile(const char* path) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    int result = S_ISREG(inode.i_mode);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return result;
}

int Ext2IsDir(const char* path) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    int result = S_ISDIR(inode.i_mode);
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return result;
}

uint64_t Ext2GetFileSize(const char* path) {
    ReadLock(&volume.lock, MLFQGetCurrentProcess()->pid);
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
        return 0;
    }

    uint64_t size = inode.i_size;
    ReadUnlock(&volume.lock, MLFQGetCurrentProcess()->pid);
    return size;
}