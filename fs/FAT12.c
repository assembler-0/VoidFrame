#include "FAT12.h"
#include "Console.h"
#include "Ide.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "MemPool.h"
#include "Serial.h"
#include "StringOps.h"

static Fat12Volume volume;
static uint8_t* sector_buffer = NULL;
int fat12_initialized = 0;  // Make global for VFS

void Fat12ConvertFilename(const char* filename, char* fat_name) {
    FastMemset(fat_name, ' ', 11);
    int name_pos = 0, ext_pos = 0, in_ext = 0;

    for (int i = 0; filename[i] && i < 64; i++) {
        if (filename[i] == '.') {
            in_ext = 1;
            ext_pos = 0;
        } else if (!in_ext && name_pos < 8) {
            fat_name[name_pos++] = (filename[i] >= 'a' && filename[i] <= 'z') ? filename[i] - 32 : filename[i];
        } else if (in_ext && ext_pos < 3) {
            fat_name[8 + ext_pos++] = (filename[i] >= 'a' && filename[i] <= 'z') ? filename[i] - 32 : filename[i];
        }
    }
}

int Fat12Init(uint8_t drive) {
    if (fat12_initialized) {
        return 0;
    }
    volume.drive = drive;

    // Read boot sector
    uint8_t boot_sector[512];
    if (IdeReadSector(drive, 0, boot_sector) != IDE_OK) {
        return -1;
    }
    // Copy the parsed fields into our struct to avoid overflow
    FastMemcpy(&volume.boot, boot_sector, sizeof(Fat12BootSector));

    // Validate sector size
    // For now we only support 512-byte ATA sectors
    if (volume.boot.bytes_per_sector != 512) {
        return -1;
    }

    // Allocate sector buffer
    sector_buffer = FastAlloc(POOL_SIZE_512);
    if (!sector_buffer) {
        return -1;
    }

    // Calculate important sectors
    volume.fat_sector = volume.boot.reserved_sectors;
    volume.root_sector = volume.fat_sector + (volume.boot.fat_count * volume.boot.sectors_per_fat);
    uint32_t root_sectors = (volume.boot.root_entries * 32 + volume.boot.bytes_per_sector - 1) / volume.boot.bytes_per_sector;
    volume.data_sector = volume.root_sector + root_sectors;

    // Allocate and load FAT table
    uint32_t fat_size = volume.boot.sectors_per_fat * 512;
    volume.fat_table = KernelMemoryAlloc(fat_size);
    if (!volume.fat_table) {
        KernelFree(sector_buffer);
        sector_buffer = NULL;
        return -1;
    }

    // Read FAT table
    for (int i = 0; i < volume.boot.sectors_per_fat; i++) {
        if (IdeReadSector(drive, volume.fat_sector + i, volume.fat_table + (i * 512)) != IDE_OK) {
            KernelFree(volume.fat_table);
            volume.fat_table = NULL;
            KernelFree(sector_buffer);
            sector_buffer = NULL;
            return -1;
        }
    }

    fat12_initialized = 1;
    return 0;
}


// Get next cluster from FAT table
static uint16_t Fat12GetNextCluster(uint16_t cluster) {
    if (cluster >= 0xFF8) return FAT12_CLUSTER_EOF;

    uint32_t fat_offset = cluster + (cluster / 2); // cluster * 1.5
    uint16_t fat_value = *(uint16_t*)&volume.fat_table[fat_offset];

    if (cluster & 1) {
        fat_value = (fat_value >> 4) & 0x0FFF; // Odd cluster, upper 12 bits
    } else {
        fat_value &= 0x0FFF; // Even cluster, lower 12 bits
    }

    return fat_value;
}

static void Fat12SetFatEntry(uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint16_t* entry = (uint16_t*)&volume.fat_table[fat_offset];

    if (cluster & 1) { // Odd cluster
        *entry = (*entry & 0x000F) | ((value & 0x0FFF) << 4);
    } else { // Even cluster
        *entry = (*entry & 0xF000) | (value & 0x0FFF);
    }
}

// Writes the in-memory FAT cache back to all FAT copies on disk
static int Fat12WriteFat() {
    for (int i = 0; i < volume.boot.fat_count; i++) {
        uint32_t fat_start = volume.boot.reserved_sectors + (i * volume.boot.sectors_per_fat);
        for (int j = 0; j < volume.boot.sectors_per_fat; j++) {
            if (IdeWriteSector(volume.drive, fat_start + j, volume.fat_table + (j * 512)) != IDE_OK) {
                return -1;
            }
        }
    }
    return 0;
}

static uint16_t Fat12FindFreeCluster() {
    uint32_t total_data_sectors = volume.boot.total_sectors_16 - volume.data_sector;
    if (volume.boot.total_sectors_16 == 0) {
        total_data_sectors = volume.boot.total_sectors_32 - volume.data_sector;
    }
    uint32_t total_clusters = total_data_sectors / volume.boot.sectors_per_cluster;

    // Start search from cluster 2
    for (uint16_t i = 2; i < (uint16_t)(2 + total_clusters); i++) {
        if (Fat12GetNextCluster(i) == FAT12_CLUSTER_FREE) {
            return i;
        }
    }
    return 0; // Invalid cluster number indicates failure
}

int Fat12GetCluster(uint16_t cluster, uint8_t* buffer) {
    if (volume.boot.sectors_per_cluster == 0 || volume.boot.sectors_per_cluster > 8) return -1;

    uint32_t sector = volume.data_sector + ((cluster - 2) * volume.boot.sectors_per_cluster);

    for (int i = 0; i < volume.boot.sectors_per_cluster; i++) {
        if (IdeReadSector(volume.drive, sector + i, buffer + (i * 512)) != IDE_OK) {
            return -1;
        }
    }

    return 0;
}

// NEW: Parse path and find directory entry
static Fat12DirEntry* Fat12FindEntry(const char* path, uint16_t* parent_cluster, uint32_t* entry_sector, int* entry_offset) {
    if (!path || path[0] != '/') return NULL;

    // Start at root directory
    uint16_t current_cluster = 0; // 0 = root directory

    // Handle root directory case
    if (FastStrlen(path, 256) == 1) { // path == "/"
        *parent_cluster = 0;
        return NULL; // Root has no entry
    }

    // Parse path components
    const char* p = path + 1; // Skip leading /
    char component[12];

    while (*p) {
        // Extract next component
        int comp_len = 0;
        while (*p && *p != '/' && comp_len < 11) {
            component[comp_len++] = *p++;
        }
        component[comp_len] = 0;
        if (*p == '/') p++;

        // Convert to FAT name
        char fat_name[11];
        Fat12ConvertFilename(component, fat_name);

        // Search in current directory
        Fat12DirEntry* found = NULL;
        uint32_t found_sector = 0;
        int found_offset = -1;

        if (current_cluster == 0) {
            // Search root directory
            uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
            for (uint32_t sector = 0; sector < root_sectors; sector++) {
                if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) {
                    return NULL;
                }

                Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
                for (int i = 0; i < 16; i++) {
                    if ((uint8_t)entries[i].name[0] == 0x00) break;
                    if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                    if (entries[i].attr & FAT12_ATTR_VOLUME_ID) continue;

                    if (FastMemcmp(entries[i].name, fat_name, 11) == 0) {
                        found = &entries[i];
                        found_sector = volume.root_sector + sector;
                        found_offset = i;
                        break;
                    }
                }
                if (found) break;
            }
        } else {
            // Search cluster-based directory
            uint16_t cluster = current_cluster;
            uint32_t cluster_bytes = volume.boot.sectors_per_cluster * 512;
            uint8_t* cluster_buffer = KernelMemoryAlloc(cluster_bytes);
            if (!cluster_buffer) return NULL;

            while (cluster < 0xFF8 && !found) {
                if (Fat12GetCluster(cluster, cluster_buffer) != 0) {
                    KernelFree(cluster_buffer);
                    return NULL;
                }

                Fat12DirEntry* entries = (Fat12DirEntry*)cluster_buffer;
                int entries_per_cluster = cluster_bytes / 32;
                for (int i = 0; i < entries_per_cluster; i++) {
                    if ((uint8_t)entries[i].name[0] == 0x00) break;
                    if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                    if (entries[i].attr & FAT12_ATTR_VOLUME_ID) continue;

                    if (FastMemcmp(entries[i].name, fat_name, 11) == 0) {
                        found = &entries[i];
                        // Calculate sector for this entry
                        found_sector = volume.data_sector + ((cluster - 2) * volume.boot.sectors_per_cluster) + (i * 32) / 512;
                        found_offset = (i * 32) % 512 / 32;
                        break;
                    }
                }
                cluster = Fat12GetNextCluster(cluster);
            }
            KernelFree(cluster_buffer);
        }

        if (!found) return NULL;

        // If this is the last component, return it
        if (!*p) {
            *parent_cluster = current_cluster;
            *entry_sector = found_sector;
            *entry_offset = found_offset;

            // Re-read the sector to return a valid pointer
            if (IdeReadSector(volume.drive, found_sector, sector_buffer) != IDE_OK) {
                return NULL;
            }
            return &((Fat12DirEntry*)sector_buffer)[found_offset];
        }

        // Must be a directory to continue
        if (!(found->attr & FAT12_ATTR_DIRECTORY)) return NULL;

        current_cluster = found->cluster_low;
    }

    return NULL;
}

// NEW: Check if a path is a directory
int Fat12IsDirectory(const char* path) {
    if (!path) return 0;

    // Root is always a directory
    if (FastStrCmp(path, "/") == 0) return 1;

    uint16_t parent_cluster;
    uint32_t entry_sector;
    int entry_offset;

    Fat12DirEntry* entry = Fat12FindEntry(path, &parent_cluster, &entry_sector, &entry_offset);
    if (!entry) return 0;

    return (entry->attr & FAT12_ATTR_DIRECTORY) ? 1 : 0;
}

// NEW: List directory contents (works for any directory)

// This is the MODIFIED Fat12ListDirectory
int Fat12ListDirectory(const char* path) {
    if (!path) return -1;

    // If the path is the root, call the dedicated root listing function
    if (FastStrCmp(path, "/") == 0) {
        return Fat12ListRoot();
    }

    // If we are here, we are listing a subdirectory, not the root.
    uint16_t parent_cluster;
    uint32_t entry_sector;
    int entry_offset;

    Fat12DirEntry* entry = Fat12FindEntry(path, &parent_cluster, &entry_sector, &entry_offset);
    if (!entry || !(entry->attr & FAT12_ATTR_DIRECTORY)) {
        // Path is not a valid directory
        return -1;
    }

    uint16_t cluster = entry->cluster_low;

    // --- The rest of the function remains the same ---
    // (The part that allocates cluster_buffer and loops through cluster chains)
    uint32_t cluster_bytes = volume.boot.sectors_per_cluster * 512;
    uint8_t* cluster_buffer = KernelMemoryAlloc(cluster_bytes);
    if (!cluster_buffer) return -1;

    uint16_t current_cluster = cluster;
    while (current_cluster < 0xFF8) {
        if (Fat12GetCluster(current_cluster, cluster_buffer) != 0) {
            KernelFree(cluster_buffer);
            return -1;
        }

        Fat12DirEntry* entries = (Fat12DirEntry*)cluster_buffer;
        int entries_per_cluster = cluster_bytes / 32;
        for (int i = 0; i < entries_per_cluster; i++) {
            Fat12DirEntry* current_entry = &entries[i];

            if ((uint8_t)current_entry->name[0] == 0x00) break;
            if ((uint8_t)current_entry->name[0] == 0xE5) continue;
            if (current_entry->attr & FAT12_ATTR_VOLUME_ID) continue;
            if (current_entry->name[0] == '.' && current_entry->name[1] == ' ' && current_entry->ext[0] == ' ') continue;
            if (current_entry->name[0] == '.' && current_entry->name[1] == '.' && current_entry->name[2] == ' ') continue;

            // (Printing logic is the same)
            PrintKernel("  ");
            for (int j = 0; j < 8 && current_entry->name[j] != ' '; j++) {
                char c[2] = {current_entry->name[j], 0};
                PrintKernel(c);
            }
            if (current_entry->ext[0] != ' ') {
                PrintKernel(".");
                for (int j = 0; j < 3 && current_entry->ext[j] != ' '; j++) {
                    char c[2] = {current_entry->ext[j], 0};
                    PrintKernel(c);
                }
            }
            if (current_entry->attr & FAT12_ATTR_DIRECTORY) {
                PrintKernel(" <DIR>");
            } else {
                PrintKernel(" ");
                PrintKernelInt(current_entry->file_size);
                PrintKernel(" bytes");
            }
            PrintKernel("\n");
        }

        current_cluster = Fat12GetNextCluster(current_cluster);
    }

    KernelFree(cluster_buffer);
    return 0;
}


int Fat12CreateDir(const char* path) {
    if (!path || path[0] != '/') return -1;

    // Extract parent directory and directory name
    char parent_path[256];
    char dir_name[256];

    // Find last slash
    int last_slash = -1;
    for (int i = FastStrlen(path, 256) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    if (last_slash <= 0) {
        // Creating in root
        FastStrCopy(parent_path, "/", 256);
        FastStrCopy(dir_name, path + 1, 256);
    } else {
        // Extract parent path and name
        FastMemcpy(parent_path, path, last_slash);
        parent_path[last_slash] = 0;
        FastStrCopy(dir_name, path + last_slash + 1, 256);
    }

    // Convert directory name to FAT format
    char fat_name[11];
    Fat12ConvertFilename(dir_name, fat_name);

    // Find parent directory
    uint16_t parent_cluster = 0;
    if (FastStrCmp(parent_path, "/") != 0) {
        uint16_t temp_parent;
        uint32_t temp_sector;
        int temp_offset;
        Fat12DirEntry* parent_entry = Fat12FindEntry(parent_path, &temp_parent, &temp_sector, &temp_offset);
        if (!parent_entry || !(parent_entry->attr & FAT12_ATTR_DIRECTORY)) return -1;
        parent_cluster = parent_entry->cluster_low;
    }

    // Check if directory already exists and find free entry
    // For simplicity, let's assume we're creating in root for now
    // Full implementation would need to handle subdirectories
    if (parent_cluster != 0) {
        // TODO: Handle creating directories in subdirectories
        return -1;
    }

    // Original root directory creation logic
    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
    uint32_t entry_sector_lba = 0;
    int entry_offset = -1;

    for (uint32_t sector_idx = 0; sector_idx < root_sectors; sector_idx++) {
        uint32_t current_lba = volume.root_sector + sector_idx;
        if (IdeReadSector(volume.drive, current_lba, sector_buffer) != IDE_OK) return -1;

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            uint8_t first_char = entries[i].name[0];
            if (first_char == 0x00 || first_char == 0xE5) {
                if (entry_offset == -1) {
                    entry_sector_lba = current_lba;
                    entry_offset = i;
                }
            } else if (FastMemcmp(entries[i].name, fat_name, 11) == 0) {
                return -1; // Directory/file with that name already exists
            }
        }
    }

    if (entry_offset == -1) return -1; // Root directory is full

    // Allocate a cluster for the new directory's data
    uint16_t new_cluster = Fat12FindFreeCluster();
    if (new_cluster == 0) return -1;

    Fat12SetFatEntry(new_cluster, FAT12_CLUSTER_EOF);

    // Create '.' and '..' entries and write to cluster
    uint32_t cluster_size_bytes = volume.boot.sectors_per_cluster * 512;
    uint8_t* cluster_buffer = KernelMemoryAlloc(cluster_size_bytes);
    if (!cluster_buffer) return -1;

    FastMemset(cluster_buffer, 0, cluster_size_bytes);

    // Create the '.' entry (points to itself)
    Fat12DirEntry* dot_entry = (Fat12DirEntry*)cluster_buffer;
    FastMemcpy(dot_entry->name, ".          ", 11);
    dot_entry->attr = FAT12_ATTR_DIRECTORY;
    dot_entry->cluster_low = new_cluster;
    dot_entry->file_size = 0;

    // Create the '..' entry (points to parent)
    Fat12DirEntry* dotdot_entry = dot_entry + 1;
    FastMemcpy(dotdot_entry->name, "..         ", 11);
    dotdot_entry->attr = FAT12_ATTR_DIRECTORY;
    dotdot_entry->cluster_low = parent_cluster; // 0 for root directory's parent
    dotdot_entry->file_size = 0;

    // Write the new directory's data cluster to disk
    uint32_t data_lba = volume.data_sector + ((new_cluster - 2) * volume.boot.sectors_per_cluster);
    for (int i = 0; i < volume.boot.sectors_per_cluster; i++) {
        if (IdeWriteSector(volume.drive, data_lba + i, cluster_buffer + (i * 512)) != IDE_OK) {
            KernelFree(cluster_buffer);
            return -1;
        }
    }
    KernelFree(cluster_buffer);

    // Update the entry in the parent directory
    if (IdeReadSector(volume.drive, entry_sector_lba, sector_buffer) != IDE_OK) return -1;

    Fat12DirEntry* new_dir_entry = &((Fat12DirEntry*)sector_buffer)[entry_offset];
    FastMemcpy(new_dir_entry->name, fat_name, 11);
    new_dir_entry->attr = FAT12_ATTR_DIRECTORY;
    new_dir_entry->cluster_low = new_cluster;
    new_dir_entry->file_size = 0;

    // Write changes back to disk
    if (IdeWriteSector(volume.drive, entry_sector_lba, sector_buffer) != IDE_OK) return -1;
    if (Fat12WriteFat() != 0) return -1;

    return 0;
}

// NEW: Enhanced file operations with path support
int Fat12ReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!path) return -1;

    uint16_t parent_cluster;
    uint32_t entry_sector;
    int entry_offset;

    Fat12DirEntry* entry = Fat12FindEntry(path, &parent_cluster, &entry_sector, &entry_offset);
    if (!entry || (entry->attr & FAT12_ATTR_DIRECTORY)) return -1;

    uint16_t cluster = entry->cluster_low;
    uint32_t bytes_read = 0;
    uint32_t file_size = entry->file_size;

    if (file_size == 0) return 0;

    uint32_t cluster_bytes = volume.boot.sectors_per_cluster * 512;
    if (cluster_bytes == 0) return -1;

    while (cluster < 0xFF8 && bytes_read < max_size) {
        uint8_t* cluster_buffer = KernelMemoryAlloc(cluster_bytes);
        if (!cluster_buffer) return -1;

        if (Fat12GetCluster(cluster, cluster_buffer) != 0) {
            KernelFree(cluster_buffer);
            return -1;
        }

        uint32_t remaining_in_file = file_size - bytes_read;
        uint32_t copy_size = (cluster_bytes < remaining_in_file) ? cluster_bytes : remaining_in_file;
        if (bytes_read + copy_size > max_size) copy_size = max_size - bytes_read;

        FastMemcpy((uint8_t*)buffer + bytes_read, cluster_buffer, copy_size);
        bytes_read += copy_size;

        KernelFree(cluster_buffer);
        cluster = Fat12GetNextCluster(cluster);
    }
    return bytes_read;
}

int Fat12CreateFile(const char* filename) {
    return Fat12WriteFile(filename, NULL, 0);
}

// Enhanced file write with path support
int Fat12WriteFile(const char* path, const void* buffer, uint32_t size) {
    if (!path) return -1;

    // Extract parent directory and filename
    char parent_path[256];
    char filename[256];

    int last_slash = -1;
    for (int i = FastStrlen(path, 256) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    if (last_slash <= 0) {
        // Creating in root
        FastStrCopy(parent_path, "/", 256);
        FastStrCopy(filename, path + 1, 256);
    } else {
        FastMemcpy(parent_path, path, last_slash);
        parent_path[last_slash] = 0;
        FastStrCopy(filename, path + last_slash + 1, 256);
    }

    // For now, only support root directory files
    if (FastStrCmp(parent_path, "/") != 0) {
        return -1; // TODO: Support subdirectory file creation
    }

    // Use original implementation for root directory
    char fat_name[11];
    Fat12ConvertFilename(filename, fat_name);

    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
    uint32_t free_sector_lba = 0;
    int free_entry_idx = -1;
    uint32_t entry_sector = 0;
    int entry_offset = -1;
    Fat12DirEntry* existing_entry = NULL;

    // Find existing entry or a free one
    for (uint32_t sector_idx = 0; sector_idx < root_sectors; sector_idx++) {
        uint32_t current_sector = volume.root_sector + sector_idx;
        if (IdeReadSector(volume.drive, current_sector, sector_buffer) != IDE_OK) return -1;

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            uint8_t first_char = entries[i].name[0];
            if (first_char == 0x00 || first_char == 0xE5) {
                if (free_entry_idx == -1) {
                    free_sector_lba = current_sector;
                    free_entry_idx = i;
                }
            } else if (!(entries[i].attr & FAT12_ATTR_VOLUME_ID)) {
                if (FastMemcmp(entries[i].name, fat_name, 11) == 0) {
                    if (entries[i].attr & FAT12_ATTR_DIRECTORY) return -1;
                    entry_sector = current_sector;
                    entry_offset = i;
                    existing_entry = &entries[i];
                    goto found_entry;
                }
            }
        }
    }
found_entry:

    if (entry_offset == -1) {
        if (free_entry_idx == -1) return -1;
        entry_sector = free_sector_lba;
        entry_offset = free_entry_idx;
    }

    if (IdeReadSector(volume.drive, entry_sector, sector_buffer) != IDE_OK) return -1;
    Fat12DirEntry* target_entry = &((Fat12DirEntry*)sector_buffer)[entry_offset];

    // If overwriting, clear the old cluster chain
    if (existing_entry) {
        uint16_t cluster = existing_entry->cluster_low;
        while (cluster >= 2 && cluster < 0xFF8) {
            uint16_t next_cluster = Fat12GetNextCluster(cluster);
            Fat12SetFatEntry(cluster, FAT12_CLUSTER_FREE);
            cluster = next_cluster;
        }
    }

    uint16_t start_cluster = 0;
    uint32_t cluster_bytes = volume.boot.sectors_per_cluster * 512;

    if (size > 0) {
        if (volume.boot.sectors_per_cluster == 0 || volume.boot.sectors_per_cluster > 8) return -1;
        uint16_t current_cluster = Fat12FindFreeCluster();
        if (current_cluster == 0) return -1;
        start_cluster = current_cluster;

        uint32_t bytes_written = 0;
        const uint8_t* buf_ptr = (const uint8_t*)buffer;

        while (bytes_written < size) {
            uint32_t to_write = size - bytes_written;
            if (to_write > cluster_bytes) to_write = cluster_bytes;

            uint8_t* cluster_buf = KernelMemoryAlloc(cluster_bytes);
            if (!cluster_buf) return -1;
            FastMemset(cluster_buf, 0, cluster_bytes);
            if (to_write > 0) {
                FastMemcpy(cluster_buf, buf_ptr + bytes_written, to_write);
            }

            uint32_t lba = volume.data_sector + ((current_cluster - 2) * volume.boot.sectors_per_cluster);
            for (int i = 0; i < volume.boot.sectors_per_cluster; i++) {
                if (IdeWriteSector(volume.drive, lba + i, cluster_buf + (i * 512)) != IDE_OK) {
                    KernelFree(cluster_buf);
                    return -1;
                }
            }
            KernelFree(cluster_buf);
            bytes_written += to_write;

            if (bytes_written < size) {
                uint16_t next_cluster = Fat12FindFreeCluster();
                if (next_cluster == 0) return -1;
                Fat12SetFatEntry(current_cluster, next_cluster);
                current_cluster = next_cluster;
            } else {
                Fat12SetFatEntry(current_cluster, 0xFFF);
            }
        }
    }

    // Update directory entry metadata
    if (!existing_entry) {
        FastMemcpy(target_entry->name, fat_name, 11);
        target_entry->attr = FAT12_ATTR_ARCHIVE;
        target_entry->create_time = 0;
        target_entry->create_date = 0;
        target_entry->cluster_high = 0;
    }
    target_entry->file_size = size;
    target_entry->cluster_low = start_cluster;

    // Write the updated directory sector and FAT back to disk
    if (IdeWriteSector(volume.drive, entry_sector, sector_buffer) != IDE_OK) return -1;
    if (Fat12WriteFat() != 0) return -1;

    return size;
}

// Enhanced file/directory deletion with path support
int Fat12DeleteFile(const char* path) {
    if (!path) return -1;

    uint16_t parent_cluster;
    uint32_t entry_sector;
    int entry_offset;

    Fat12DirEntry* entry = Fat12FindEntry(path, &parent_cluster, &entry_sector, &entry_offset);
    if (!entry) return -1;

    // If it's a directory, check if it's empty (only . and .. entries)
    if (entry->attr & FAT12_ATTR_DIRECTORY) {
        uint16_t dir_cluster = entry->cluster_low;
        if (dir_cluster >= 2) {
            uint32_t cluster_bytes = volume.boot.sectors_per_cluster * 512;
            uint8_t* cluster_buffer = KernelMemoryAlloc(cluster_bytes);
            if (!cluster_buffer) return -1;

            // Check if directory is empty (only . and .. entries)
            if (Fat12GetCluster(dir_cluster, cluster_buffer) != 0) {
                KernelFree(cluster_buffer);
                return -1;
            }

            Fat12DirEntry* entries = (Fat12DirEntry*)cluster_buffer;
            int entries_per_cluster = cluster_bytes / 32;
            int valid_entries = 0;

            for (int i = 0; i < entries_per_cluster; i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) break;
                if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr & FAT12_ATTR_VOLUME_ID) continue;

                // Count entries that are not . or ..
                if (!(FastMemcmp(entries[i].name, ".          ", 11) == 0 ||
                      FastMemcmp(entries[i].name, "..         ", 11) == 0)) {
                    valid_entries++;
                }
            }

            KernelFree(cluster_buffer);

            if (valid_entries > 0) {
                return -1; // Directory not empty
            }
        }
    }

    // Free the cluster chain
    uint16_t cluster = entry->cluster_low;
    while (cluster >= 2 && cluster < 0xFF8) {
        uint16_t next_cluster = Fat12GetNextCluster(cluster);
        Fat12SetFatEntry(cluster, FAT12_CLUSTER_FREE);
        cluster = next_cluster;
    }

    // Mark directory entry as deleted
    if (IdeReadSector(volume.drive, entry_sector, sector_buffer) != IDE_OK) {
        return -1;
    }

    Fat12DirEntry* target_entry = &((Fat12DirEntry*)sector_buffer)[entry_offset];
    target_entry->name[0] = 0xE5;

    // Write changes back to disk
    if (IdeWriteSector(volume.drive, entry_sector, sector_buffer) != IDE_OK) {
        return -1;
    }

    if (Fat12WriteFat() != 0) {
        return -1;
    }

    return 0;
}

// Keep original functions for backward compatibility
// THIS IS THE NEW, CORRECTED Fat12ListRoot
int Fat12ListRoot(void) {
    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;

    for (uint32_t sector = 0; sector < root_sectors; sector++) {
        if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) {
            PrintKernel("Error reading root directory sector.\n");
            return -1;
        }

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        // The root directory has 16 entries per 512-byte sector
        for (int i = 0; i < 16; i++) {
            Fat12DirEntry* entry = &entries[i];

            // 0x00 means end of directory
            if ((uint8_t)entry->name[0] == 0x00) break;
            // 0xE5 means entry is deleted
            if ((uint8_t)entry->name[0] == 0xE5) continue;
            // Skip volume label entries
            if (entry->attr & FAT12_ATTR_VOLUME_ID) continue;

            // --- Printing Logic (copied from your Fat12ListDirectory) ---
            PrintKernel("  ");
            for (int j = 0; j < 8 && entry->name[j] != ' '; j++) {
                char c[2] = {entry->name[j], 0};
                PrintKernel(c);
            }

            if (entry->ext[0] != ' ') {
                PrintKernel(".");
                for (int j = 0; j < 3 && entry->ext[j] != ' '; j++) {
                    char c[2] = {entry->ext[j], 0};
                    PrintKernel(c);
                }
            }

            if (entry->attr & FAT12_ATTR_DIRECTORY) {
                PrintKernel(" <DIR>");
            } else {
                PrintKernel(" ");
                PrintKernelInt(entry->file_size);
                PrintKernel(" bytes");
            }
            PrintKernel("\n");
        }
    }
    return 0;
}