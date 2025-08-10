#include "FAT12.h"
#include "Console.h"
#include "Ide.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "MemPool.h"
static Fat12Volume volume;
static uint8_t* sector_buffer = NULL;
int fat12_initialized = 0;  // Make global for VFS

// Forward-declare IdeWriteSector, assuming it exists in Ide.h
int IdeWriteSector(uint8_t drive, uint32_t lba, const uint8_t* buffer);

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
        fat_value >>= 4; // Odd cluster, upper 12 bits
    } else {
        fat_value &= 0xFFF; // Even cluster, lower 12 bits
    }

    return fat_value;
}

static void Fat12SetFatEntry(uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint16_t* entry = (uint16_t*)&volume.fat_table[fat_offset];

    if (cluster & 1) { // Odd cluster
        *entry = (*entry & 0x000F) | (value << 4);
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
    for (uint16_t i = 2; i < total_clusters; i++) {
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

int Fat12ListRoot(void) {
    PrintKernel("[SYSTEM] Root directory contents:\n");

    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;

    for (uint32_t sector = 0; sector < root_sectors; sector++) {
        if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) {
            PrintKernelError("[SYSTEM] Failed to read root directory\n");
            return -1;
        }

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) { // 16 entries per sector
            Fat12DirEntry* entry = &entries[i];

            if ((uint8_t)entry->name[0] == 0x00) break;
            if ((uint8_t)entry->name[0] == 0xE5) continue; // Deleted
            if (entry->attr & FAT12_ATTR_VOLUME_ID) continue; // Volume label

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

static void Fat12ConvertFilename(const char* filename, char* fat_name) {
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

int Fat12ReadFile(const char* filename, void* buffer, uint32_t max_size) {
    char fat_name[11];
    Fat12ConvertFilename(filename, fat_name);

    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;

    for (uint32_t sector = 0; sector < root_sectors; sector++) {
        if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) return -1;

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            Fat12DirEntry* entry = &entries[i];

            if ((uint8_t)entry->name[0] == 0x00) return -1;
            if ((uint8_t)entry->name[0] == 0xE5) continue;
            if (entry->attr & (FAT12_ATTR_DIRECTORY | FAT12_ATTR_VOLUME_ID)) continue;

            if (FastMemcmp(entry->name, fat_name, 11) == 0) {
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
        }
    }
    return -1; // File not found
}

int Fat12CreateFile(const char* filename) {
    // A zero-byte write will create an empty file
    return Fat12WriteFile(filename, NULL, 0);
}

int Fat12WriteFile(const char* filename, const void* buffer, uint32_t size) {
    char fat_name[11];
    Fat12ConvertFilename(filename, fat_name);

    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
    uint32_t entry_sector = 0;
    int entry_offset = -1;
    Fat12DirEntry* existing_entry = NULL;

    // Find existing entry or a free one
    for (uint32_t sector_idx = 0; sector_idx < root_sectors && entry_sector == 0; sector_idx++) {
        uint32_t current_sector = volume.root_sector + sector_idx;
        if (IdeReadSector(volume.drive, current_sector, sector_buffer) != IDE_OK) return -1;

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            uint8_t first_char = entries[i].name[0];
            if (first_char == 0x00 || first_char == 0xE5) { // Free or deleted
                if (entry_offset == -1) { // Found the first free spot
                    entry_sector = current_sector;
                    entry_offset = i;
                }
            } else if (!(entries[i].attr & FAT12_ATTR_VOLUME_ID)) {
                if (FastMemcmp(entries[i].name, fat_name, 11) == 0) {
                    if (entries[i].attr & FAT12_ATTR_DIRECTORY) return -1; // Can't write to dir
                    entry_sector = current_sector;
                    entry_offset = i;
                    existing_entry = &entries[i];
                    goto found_entry;
                }
            }
        }
    }
found_entry:

    if (entry_offset == -1) return -1; // Directory full

    // Re-read sector in case buffer was used for other sectors
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
        uint16_t current_cluster = Fat12FindFreeCluster();
        if (current_cluster == 0) return -1; // Out of space
        start_cluster = current_cluster;

        uint32_t bytes_written = 0;
        const uint8_t* buf_ptr = (const uint8_t*)buffer;

        while (bytes_written < size) {
            uint32_t to_write = size - bytes_written;
            if (to_write > cluster_bytes) to_write = cluster_bytes;

            uint8_t* cluster_buf = KernelMemoryAlloc(cluster_bytes);
            if (!cluster_buf) return -1; // Out of memory
            FastMemcpy(cluster_buf, buf_ptr + bytes_written, to_write);

            uint32_t lba = volume.data_sector + ((current_cluster - 2) * volume.boot.sectors_per_cluster);
            for (int i = 0; i < volume.boot.sectors_per_cluster; i++) {
                if (IdeWriteSector(volume.drive, lba + i, cluster_buf + (i * 512)) != IDE_OK) {
                    KernelFree(cluster_buf);
                    return -1; // Disk write error
                }
            }
            KernelFree(cluster_buf);
            bytes_written += to_write;

            if (bytes_written < size) {
                uint16_t next_cluster = Fat12FindFreeCluster();
                if (next_cluster == 0) return -1; // Out of space mid-write
                Fat12SetFatEntry(current_cluster, next_cluster);
                current_cluster = next_cluster;
            } else {
                Fat12SetFatEntry(current_cluster, 0xFFF); // EOF
            }
        }
    }

    // Update directory entry metadata
    if (!existing_entry) {
        FastMemcpy(target_entry->name, fat_name, 11);
        target_entry->attr = FAT12_ATTR_ARCHIVE; // Default attribute
        target_entry->create_time = 0; // ToDo: Implement time
        target_entry->create_date = 0;
        target_entry->cluster_high = 0;
    }
    target_entry->file_size = size;
    target_entry->cluster_low = start_cluster;
    // ToDo: Update modify time

    // Write the updated directory sector and FAT back to disk
    if (IdeWriteSector(volume.drive, entry_sector, sector_buffer) != IDE_OK) return -1;
    if (Fat12WriteFat() != 0) return -1;

    return size;
}

int Fat12DeleteFile(const char* filename) {
    char fat_name[11];
    Fat12ConvertFilename(filename, fat_name);

    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;

    for (uint32_t sector_idx = 0; sector_idx < root_sectors; sector_idx++) {
        uint32_t current_sector_lba = volume.root_sector + sector_idx;
        if (IdeReadSector(volume.drive, current_sector_lba, sector_buffer) != IDE_OK) {
            return -1; // Failed to read directory
        }

        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            Fat12DirEntry* entry = &entries[i];

            // We only care about valid, non-directory entries
            if ((uint8_t)entry->name[0] == 0x00) continue;
            if ((uint8_t)entry->name[0] == 0xE5) continue;
            if (entry->attr & (FAT12_ATTR_DIRECTORY | FAT12_ATTR_VOLUME_ID)) continue;

            if (FastMemcmp(entry->name, fat_name, 11) == 0) {
                // Found the file entry.

                // Step 1: Free the cluster chain in the FAT
                uint16_t cluster = entry->cluster_low;
                while (cluster >= 2 && cluster < 0xFF8) {
                    uint16_t next_cluster = Fat12GetNextCluster(cluster);
                    Fat12SetFatEntry(cluster, FAT12_CLUSTER_FREE);
                    cluster = next_cluster;
                }

                // Step 2: Mark the directory entry as deleted (0xE5)
                entry->name[0] = 0xE5;

                // Step 3: Write the changes back to disk
                if (IdeWriteSector(volume.drive, current_sector_lba, sector_buffer) != IDE_OK) {
                    // This is bad, we've only partially deleted.
                    // A real OS would need recovery logic here.
                    return -1;
                }

                if (Fat12WriteFat() != 0) {
                    return -1; // Failed to write updated FAT
                }

                return 0; // Success!
            }
        }
    }

    return -1; // File not found
}