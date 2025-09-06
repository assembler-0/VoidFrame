#include "Ext2.h"
#include "../VFS.h"
#include "../../kernel/etc/Console.h"
#include "../../drivers/Ide.h"
#include "../../mm/KernelHeap.h"
#include "../../mm/MemOps.h"
#include "../../kernel/etc/StringOps.h"

#define EXT2_SUPERBLOCK_OFFSET 1024
#define EXT2_MAGIC 0xEF53

typedef struct {
    uint8_t drive;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t num_groups;
    Ext2Superblock superblock;
    Ext2GroupDesc* group_descs;
} Ext2Volume;

static Ext2Volume volume;
int ext2_initialized = 0;

// Helper to read a block from the disk
int Ext2ReadBlock(uint32_t block, void* buffer) {
    if (block >= volume.superblock.s_blocks_count) {
        PrintKernelF("[EXT2] Block %u out of bounds (max: %u)\
",
                     block, volume.superblock.s_blocks_count - 1);
        return -1;
    }
    uint32_t sector_start = block * (volume.block_size / 512);
    uint32_t num_sectors   = volume.block_size / 512;
    for (uint32_t i = 0; i < num_sectors; i++) {
        if (IdeReadSector(volume.drive,
                          sector_start + i,
                          (uint8_t*)buffer + (i * 512)) != IDE_OK) {
            return -1;
        }
    }
    return 0;
}

static int Ext2WriteBlock(uint32_t block, const void* buffer) {
    if (block >= volume.superblock.s_blocks_count) {
        PrintKernelF("[EXT2] Block %u out of bounds (max: %u)\
",
                     block, volume.superblock.s_blocks_count - 1);
        return -1;
    }
    uint32_t sector_start = block * (volume.block_size / 512);
    uint32_t num_sectors   = volume.block_size / 512;
    for (uint32_t i = 0; i < num_sectors; i++) {
        if (IdeWriteSector(volume.drive,
                           sector_start + i,
                           (uint8_t*)buffer + (i * 512)) != IDE_OK) {
            return -1;
        }
    }
    return 0;
}

int Ext2Init(uint8_t drive) {
    if (ext2_initialized) {
        return 0;
    }
    volume.drive = drive;

    // The superblock is 1024 bytes long and located at offset 1024.
    // We assume 512-byte sectors, so we need to read 2 sectors starting from sector 2.
    uint8_t sb_buffer[1024];
    if (IdeReadSector(drive, 2, sb_buffer) != IDE_OK || IdeReadSector(drive, 3, sb_buffer + 512) != IDE_OK) {
        PrintKernelF("[EXT2] Failed to read superblock.\n");
        return -1;
    }

    FastMemcpy(&volume.superblock, sb_buffer, sizeof(Ext2Superblock));

    // Check for EXT2 magic number
    if (volume.superblock.s_magic != EXT2_MAGIC) {
        PrintKernelF("[EXT2] Invalid magic number. Not an EXT2 filesystem.\n");
        return -1;
    }

    // Calculate important values
    if (volume.superblock.s_log_block_size > 10) {  // Max 1MB blocks
        PrintKernelF("[EXT2] Invalid block size shift: %u\n",
                     volume.superblock.s_log_block_size);
        return -1;
    }
    volume.block_size = 1024 << volume.superblock.s_log_block_size;
    volume.inode_size = volume.superblock.s_inode_size;
    volume.blocks_per_group = volume.superblock.s_blocks_per_group;
    volume.inodes_per_group = volume.superblock.s_inodes_per_group;
    if (volume.blocks_per_group == 0) {
        PrintKernelF("[EXT2] Invalid blocks_per_group: 0\n");
        return -1;
    }
    volume.num_groups = (volume.superblock.s_blocks_count + volume.blocks_per_group - 1) / volume.blocks_per_group;

    PrintKernelF("[EXT2] Block size: %d bytes\n", volume.block_size);
    PrintKernelF("[EXT2] Inode size: %d bytes\n", volume.inode_size);
    PrintKernelF("[EXT2] Block groups: %d\n", volume.num_groups);

    // Read Block Group Descriptor Table
    uint32_t bgdt_size = volume.num_groups * sizeof(Ext2GroupDesc);
    volume.group_descs = KernelMemoryAlloc(bgdt_size);
    if (!volume.group_descs) {
        PrintKernelF("[EXT2] Failed to allocate memory for BGD table.\n");
        return -1;
    }

    uint32_t bgdt_block = (volume.block_size == 1024) ? 2 : 1;
    if (Ext2ReadBlock(bgdt_block, volume.group_descs) != 0) {
        PrintKernelF("[EXT2] Failed to read BGD table.\n");
        KernelFree(volume.group_descs);
        return -1;
    }

    PrintKernelSuccess("[EXT2] Filesystem initialized successfully.\n");
    ext2_initialized = 1;
    return 0;
}

int Ext2ReadInode(uint32_t inode_num, Ext2Inode* inode) {
    if (inode_num == 0) return -1;

    uint32_t group = (inode_num - 1) / volume.inodes_per_group;
    if (group >= volume.num_groups) return -1;

    uint32_t index = (inode_num - 1) % volume.inodes_per_group;
    uint32_t inode_table_block = volume.group_descs[group].bg_inode_table;

    uint32_t block_offset = (index * volume.inode_size) / volume.block_size;
    uint32_t offset_in_block = (index * volume.inode_size) % volume.block_size;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return -1;

    if (Ext2ReadBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        return -1;
    }

    FastMemcpy(inode, block_buffer + offset_in_block, sizeof(Ext2Inode));

    KernelFree(block_buffer);
    return 0;
}

static int Ext2WriteInode(uint32_t inode_num, Ext2Inode* inode) {
    if (inode_num == 0) return -1;

    uint32_t group = (inode_num - 1) / volume.inodes_per_group;
    if (group >= volume.num_groups) return -1;

    uint32_t index = (inode_num - 1) % volume.inodes_per_group;
    uint32_t inode_table_block = volume.group_descs[group].bg_inode_table;

    uint32_t block_offset = (index * volume.inode_size) / volume.block_size;
    uint32_t offset_in_block = (index * volume.inode_size) % volume.block_size;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return -1;

    // Read-modify-write the block containing the inode
    if (Ext2ReadBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        return -1;
    }

    FastMemcpy(block_buffer + offset_in_block, inode, sizeof(Ext2Inode));

    if (Ext2WriteBlock(inode_table_block + block_offset, block_buffer) != 0) {
        KernelFree(block_buffer);
        return -1;
    }

    KernelFree(block_buffer);
    return 0;
}

// Find a directory entry in a directory inode
uint32_t Ext2FindInDir(Ext2Inode* dir_inode, const char* name) {
    if (!S_ISDIR(dir_inode->i_mode)) {
        return 0; // Not a directory
    }

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return 0;

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
                    return inode_num;
                }
            }
            offset += entry->rec_len;
            entry = (Ext2DirEntry*)((uint8_t*)block_buffer + offset);
        }
    }

    KernelFree(block_buffer);
    return 0; // Not found
}

uint32_t Ext2PathToInode(const char* path) {
    if (!ext2_initialized || !path) {
        return 0;
    }

    if (path[0] == '/' && (path[1] == '\0' || path[1] == ' ')) {
        return 2; // Root directory inode
    }

    // Start from root inode
    uint32_t current_inode_num = 2;
    Ext2Inode current_inode;
    if (Ext2ReadInode(current_inode_num, &current_inode) != 0) {
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
            return 0; // Not a directory, but path continues
        }

        current_inode_num = Ext2FindInDir(&current_inode, component);
        if (current_inode_num == 0) {
            return 0; // Component not found
        }

        if (Ext2ReadInode(current_inode_num, &current_inode) != 0) {
            return 0; // Failed to read next inode
        }

        if (*p == '/') p++;
    }

    return current_inode_num;
}

int Ext2ReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!ext2_initialized) return -1;

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) return -1; // Not found

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) {
        return -1; // Failed to read inode
    }

    if (!S_ISREG(inode.i_mode)) {
        return -1; // Not a regular file
    }

    uint32_t file_size = inode.i_size;
    uint32_t bytes_to_read = (file_size < max_size) ? file_size : max_size;
    uint32_t bytes_read = 0;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return -1;

    // Only handle direct blocks for now
    for (int i = 0; i < 12 && bytes_read < bytes_to_read; i++) {
        if (inode.i_block[i] == 0) continue;

        if (Ext2ReadBlock(inode.i_block[i], block_buffer) != 0) {
            KernelFree(block_buffer);
            return -1;
        }

        uint32_t remaining_in_block = volume.block_size;
        uint32_t remaining_in_file = bytes_to_read - bytes_read;
        uint32_t copy_size = (remaining_in_block < remaining_in_file) ? remaining_in_block : remaining_in_file;

        FastMemcpy((uint8_t*)buffer + bytes_read, block_buffer, copy_size);
        bytes_read += copy_size;
    }

    KernelFree(block_buffer);
    return bytes_read;
}

int Ext2WriteFile(const char* path, const void* buffer, uint32_t size) {
    if (!ext2_initialized) return -1;

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) {
        PrintKernelF("[EXT2] WriteFile: File not found: %s\n", path);
        return -1;
    }

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) return -1;

    if (!S_ISREG(inode.i_mode)) return -1;

    uint32_t bytes_written = 0;
    const uint8_t* data_buffer = (const uint8_t*)buffer;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return -1;

    // Write to direct blocks, overwriting existing data.
    // Does not support extending the file (allocating new blocks).
    for (int i = 0; i < 12; i++) {
        if (bytes_written >= size) break;
        if (inode.i_block[i] == 0) {
            PrintKernelF("[EXT2] WriteFile: Reached end of allocated blocks for %s. File extension not yet supported.\n", path);
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

    if (bytes_written != -1 && bytes_written > 0) {
        if (bytes_written != inode.i_size) {
             inode.i_size = bytes_written;
             // inode.i_mtime = get_current_time(); // TODO: Implement time retrieval
             if (Ext2WriteInode(inode_num, &inode) != 0) {
                 return -1; // Failed to update inode
             }
        }
    }

    if (bytes_written == -1) return -1;

    return bytes_written;
}

int Ext2ListDir(const char* path) {
    if (!ext2_initialized) return -1;

    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) return -1;

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) return -1;

    if (!S_ISDIR(inode.i_mode)) return -1;

    uint8_t* block_buffer = KernelMemoryAlloc(volume.block_size);
    if (!block_buffer) return -1;

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
    return 0;
}

int Ext2CreateFile(const char* path) {
    PrintKernelF("[EXT2] CreateFile: %s (Not implemented)\n", path);
    return -1;
}

int Ext2CreateDir(const char* path) {
    PrintKernelF("[EXT2] CreateDir: %s (Not implemented)\n", path);
    return -1;
}

int Ext2Delete(const char* path) {
    PrintKernelF("[EXT2] Delete: %s (Not implemented)\n", path);
    return -1;
}

int Ext2IsFile(const char* path) {
    if (!ext2_initialized) return 0;
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) return 0;

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) return 0;

    return S_ISREG(inode.i_mode);
}

int Ext2IsDir(const char* path) {
    if (!ext2_initialized) return 0;
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) return 0;

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) return 0;

    return S_ISDIR(inode.i_mode);
}

uint64_t Ext2GetFileSize(const char* path) {
    if (!ext2_initialized) return 0;
    uint32_t inode_num = Ext2PathToInode(path);
    if (inode_num == 0) return 0;

    Ext2Inode inode;
    if (Ext2ReadInode(inode_num, &inode) != 0) return 0;

    return inode.i_size;
}
