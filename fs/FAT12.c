#include "FAT12.h"
#include "Console.h"
#include "Ide.h"
#include "KernelHeap.h"
#include "MemOps.h"

static Fat12Volume volume;
static uint8_t* sector_buffer = NULL;
static int fat12_initialized = 0;

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
    sector_buffer = KernelMemoryAlloc(512);
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
        return -1;
    }
    
    // Read FAT table
    for (int i = 0; i < volume.boot.sectors_per_fat; i++) {
        if (IdeReadSector(drive, volume.fat_sector + i, volume.fat_table + (i * 512)) != IDE_OK) {
            KernelFree(volume.fat_table);
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

int Fat12GetCluster(uint16_t cluster, uint8_t* buffer) {
    if (cluster < 2 || cluster >= 0xFF8) return -1;
    
    uint32_t sector = volume.data_sector + ((cluster - 2) * volume.boot.sectors_per_cluster);
    
    for (int i = 0; i < volume.boot.sectors_per_cluster; i++) {
        if (IdeReadSector(volume.drive, sector + i, buffer + (i * 512)) != IDE_OK) {
            return -1;
        }
    }
    
    return 0;
}

int Fat12ListRoot(void) {
    PrintKernel("[FAT12] Root directory contents:\n");
    
    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
    
    for (uint32_t sector = 0; sector < root_sectors; sector++) {
        if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) {
            PrintKernelError("[FAT12] Failed to read root directory\n");
            return -1;
        }
        
        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) { // 16 entries per sector
            Fat12DirEntry* entry = &entries[i];
            
            // Skip empty entries
            if ((uint8_t)entry->name[0] == 0x00) break;
            if ((uint8_t)entry->name[0] == 0xE5) continue; // Deleted
            if (entry->attr & FAT12_ATTR_VOLUME_ID) continue; // Volume label
            
            // Print filename
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

int Fat12ReadFile(const char* filename, void* buffer, uint32_t max_size) {
    // Convert filename to 8.3 format
    char fat_name[11];
    FastMemset(fat_name, ' ', 11);
    
    int name_pos = 0;
    int in_ext = 0;
    int ext_pos = 0;
    
    for (int i = 0; filename[i] && i < 64; i++) {
        if (filename[i] == '.') {
            in_ext = 1;
            ext_pos = 0;
        } else if (!in_ext && name_pos < 8) {
            fat_name[name_pos++] = filename[i] >= 'a' ? filename[i] - 32 : filename[i]; // Convert to uppercase
        } else if (in_ext && ext_pos < 3) {
            fat_name[8 + ext_pos++] = filename[i] >= 'a' ? filename[i] - 32 : filename[i]; // Convert to uppercase
        }
    }
    
    // Search root directory
    uint32_t root_sectors = (volume.boot.root_entries * 32 + 511) / 512;
    
    for (uint32_t sector = 0; sector < root_sectors; sector++) {
        if (IdeReadSector(volume.drive, volume.root_sector + sector, sector_buffer) != IDE_OK) {
            return -1;
        }
        
        Fat12DirEntry* entries = (Fat12DirEntry*)sector_buffer;
        for (int i = 0; i < 16; i++) {
            Fat12DirEntry* entry = &entries[i];
            
            if ((uint8_t)entry->name[0] == 0x00) return -1; // End of directory
            if ((uint8_t)entry->name[0] == 0xE5) continue; // Deleted
            if (entry->attr & (FAT12_ATTR_DIRECTORY | FAT12_ATTR_VOLUME_ID)) continue;
            
            // Compare filename
            if (FastMemcmp(entry->name, fat_name, 11) == 0) {
                // Found file, read it
                uint16_t cluster = entry->cluster_low;
                uint32_t bytes_read = 0;
                uint8_t* buf_ptr = (uint8_t*)buffer;
                
                while (cluster < 0xFF8 && bytes_read < max_size) {
                    uint8_t cluster_buffer[512 * 8]; // Max cluster size
                    if (Fat12GetCluster(cluster, cluster_buffer) != 0) {
                        return -1;
                    }
                    
                    uint32_t cluster_size = volume.boot.sectors_per_cluster * 512;
                    uint32_t copy_size = (bytes_read + cluster_size > max_size) ? 
                                        (max_size - bytes_read) : cluster_size;
                    
                    FastMemcpy(buf_ptr + bytes_read, cluster_buffer, copy_size);
                    bytes_read += copy_size;
                    
                    cluster = Fat12GetNextCluster(cluster);
                }
                
                return bytes_read;
            }
        }
    }
    
    return -1; // File not found
}