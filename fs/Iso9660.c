#include "Iso9660.h"

#include "../drivers/Ide.h"
#include "../kernel/etc/StringOps.h"
#include "../mm/KernelHeap.h"
#include "Console.h"
#include "Format.h"
#include "MemOps.h"
#include "VFS.h"

#define ISO9660_SECTOR_SIZE 2048

static uint8_t cdrom_drive = 0xFF; // Auto-detect CD-ROM drive

// Function to detect CD-ROM drive
static int DetectCdromDrive(void) {
    if (cdrom_drive != 0xFF) return cdrom_drive;
    
    PrintKernel("[ISO] Detecting CD-ROM...\n");
    uint8_t sector_buffer[512];
    
    for (uint8_t drive = 0; drive < 4; drive++) {
        char model[41];
        if (IdeGetDriveInfo(drive, model) == IDE_OK) {
            PrintKernelF("[ISO] Drive %d: %s\n", drive, model);
        } else {
            continue;
        }
        
        // Test reading sector 64 (16*4) for PVD
        int result = IdeReadSector(drive, 64, sector_buffer);
        PrintKernelF("[ISO] Drive %d sector 64 result: %d\n", drive, result);
        
        if (result == 0) {
            PrintKernel("[ISO] Data: ");
            for (int i = 0; i < 16; i++) {
                PrintKernelF("%02X ", sector_buffer[i]);
            }
            PrintKernel("\n");
            
            if (sector_buffer[0] == 1 && FastMemcmp(sector_buffer + 1, "CD001", 5) == 0) {
                cdrom_drive = drive;
                PrintKernelF("[ISO] CD-ROM found on drive %d\n", drive);
                return drive;
            }
        }
    }
    return -1;
}

// Function to read a 2048-byte ISO sector
static int ReadSector(uint32_t lba, void* buffer) {
    int drive = DetectCdromDrive();
    if (drive < 0) {
        PrintKernel("[ISO] No CD-ROM available\n");
        return -1;
    }
    
    PrintKernelF("[ISO] Reading LBA %u from drive %d\n", lba, drive);
    
    // ISO uses 2048-byte sectors, IDE uses 512-byte sectors
    uint32_t start_sector = lba * 4;
    
    for (int i = 0; i < 4; i++) {
        int result = IdeReadSector(drive, start_sector + i, (uint8_t*)buffer + (i * 512));
        if (result != 0) {
            PrintKernelF("[ISO] Failed sector %u (part %d)\n", start_sector + i, i);
            return -1;
        }
    }
    return 0;
}

static Iso9660DirEntry* FindFileInDir(uint32_t dir_lba, uint32_t dir_size, const char* filename) {
    uint8_t* sector_buffer = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
    if (!sector_buffer) {
        return NULL;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < dir_size) {
        if (ReadSector(dir_lba + (bytes_read / ISO9660_SECTOR_SIZE), sector_buffer) != 0) {
            KernelFree(sector_buffer);
            return NULL;
        }

        Iso9660DirEntry* entry = (Iso9660DirEntry*)sector_buffer;
        uint32_t sector_offset = 0;
        char entry_filename[256];
        while (sector_offset < ISO9660_SECTOR_SIZE) {
            if (entry->length == 0) {
                // End of directory entries in this sector
                break;
            }
            FastMemset(entry_filename, 0, 256);
            uint32_t name_len = entry->file_id_length;
            if (name_len >= sizeof(entry_filename))
                name_len = sizeof(entry_filename) - 1;
            FastMemcpy(entry_filename, entry->file_id, name_len);
            entry_filename[name_len] = 0;
            PrintKernelF("[ISO] Found entry: '%s' (looking for '%s')\n", entry_filename, filename);
            char* semicolon = FastStrChr(entry_filename, ';');
            if (semicolon) {
                *semicolon = 0;
            }

            if (FastStrCmp(entry_filename, filename) == 0) {
                // Found it! We need to copy the entry to a new buffer, as the sector_buffer will be freed.
                Iso9660DirEntry* result = KernelMemoryAlloc(entry->length);
                if (result) {
                    FastMemcpy(result, entry, entry->length);
                }
                KernelFree(sector_buffer);
                return result;
            }

            sector_offset += entry->length;
            entry = (Iso9660DirEntry*)((uint8_t*)entry + entry->length);
        }
        bytes_read += ISO9660_SECTOR_SIZE;
    }

    KernelFree(sector_buffer);
    return NULL;
}


int Iso9660Read(const char* path, void* buffer, uint32_t max_size) {
    // Allocate a buffer for one sector
    PrintKernelF("ISO9660: Reading '%s'\n", path);
    uint8_t* sector_buffer = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
    if (!sector_buffer) {
        PrintKernelError("Out of memory\n");
        return -1; // Out of memory
    }

    // Find the Primary Volume Descriptor (PVD) at LBA 16
    PrintKernel("[ISO] Looking for Primary Volume Descriptor...\n");
    Iso9660Pvd* pvd = NULL;
    
    if (ReadSector(16, sector_buffer) != 0) {
        KernelFree(sector_buffer);
        PrintKernelError("[ISO] Failed to read PVD sector 16\n");
        return -1;
    }
    
    // Debug: Show first 16 bytes of sector
    PrintKernel("[ISO] First 16 bytes of sector 16: ");
    for (int i = 0; i < 16; i++) {
        PrintKernelF("%02X ", sector_buffer[i]);
    }
    PrintKernel("\n");
    
    Iso9660Pvd* cand = (Iso9660Pvd*)sector_buffer;
    PrintKernelF("[ISO] PVD type: %d, ID: %.5s\n", cand->type, cand->id);
    
    if (cand->type == 1 && FastMemcmp(cand->id, "CD001", 5) == 0) {
        PrintKernel("[ISO] Found valid PVD!\n");
        pvd = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
        if (!pvd) {
            KernelFree(sector_buffer);
            PrintKernelError("[ISO] Out of memory for PVD\n");
            return -1;
        }
        FastMemcpy(pvd, sector_buffer, ISO9660_SECTOR_SIZE);
    } else {
        PrintKernelError("[ISO] Invalid PVD signature\n");
    }
    KernelFree(sector_buffer);

    if (!pvd) {
        PrintKernelError("[ISO] PVD not found - not a valid ISO9660 filesystem\n");
        return -1;
    }
    
    PrintKernel("[ISO] PVD found successfully\n");

    // Get the root directory entry
    Iso9660DirEntry* root_entry = (Iso9660DirEntry*)pvd->root_directory_record;

    // Traverse the path
    char* path_copy = KernelMemoryAlloc(FastStrlen(path, 256) + 1);
    if (!path_copy) {
        KernelFree(pvd);
        return -1;
    }
    FastStrCopy(path_copy, path, 256);

    char* current_part = path_copy;
    Iso9660DirEntry* current_entry = root_entry;
    uint32_t current_lba = root_entry->extent_loc_le;
    uint32_t current_size = root_entry->data_length_le;

    // If path is just "/", we can't do much, but let's not crash
    if (FastStrCmp(path, "/") == 0) {
        KernelFree(path_copy);
        KernelFree(pvd);
        PrintKernelError("Nothing to read\n");
        return 0; // Not an error, but nothing to read
    }

    while (*current_part) {
        if (*current_part == '/') {
            current_part++;
            continue;
        }

        char* next_slash = FastStrChr(current_part, '/');
        if (next_slash) {
            *next_slash = 0;
        }

        Iso9660DirEntry* found_entry = FindFileInDir(current_lba, current_size, current_part);
        if (current_entry != root_entry) {
            KernelFree(current_entry);
        }
        current_entry = found_entry;

        if (!current_entry) {
            KernelFree(path_copy);
            KernelFree(pvd);
            PrintKernelError("Path not found\n");
            return -1; // Path not found
        }

        current_lba = current_entry->extent_loc_le;
        current_size = current_entry->data_length_le;

        if (next_slash) {
            current_part = next_slash + 1;
        } else {
            break;
        }
    }

    KernelFree(path_copy);

    if (!current_entry) {
        KernelFree(pvd);
        PrintKernelError("This should not happen\n");
        return -1; // Should not happen
    }

    // Read the file data
    if (current_entry->file_flags & 2) { // directory bit
        if (current_entry != root_entry) {
            KernelFree(current_entry);
        }
        KernelFree(pvd);
        PrintKernelError("Path is a directory\n");
        return -1;
    }
    PrintKernelF("[ISO] Found file: data_length_le=%u, extent_loc_le=%u\n",
             current_entry->data_length_le, current_entry->extent_loc_le);
    // Read the file data
    const uint32_t file_size = current_entry->data_length_le;
    const uint32_t file_lba  = current_entry->extent_loc_le;
    // Stat-only mode
    if (buffer == NULL || max_size == 0) {
        if (current_entry != root_entry) {
            KernelFree(current_entry);
        }
        KernelFree(pvd);
        return (int)file_size;
    }

    const uint32_t to_read = (file_size < max_size) ? file_size : max_size;
    uint8_t* read_buffer = (uint8_t*)buffer;
    uint32_t bytes_read = 0;
    while (bytes_read < to_read) {
        uint32_t sector_to_read = file_lba + (bytes_read / ISO9660_SECTOR_SIZE);
        uint8_t* temp_sector = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
        if (!temp_sector) {
            if (current_entry != root_entry) KernelFree(current_entry);
            KernelFree(pvd);
            return -1;
        }

        if (ReadSector(sector_to_read, temp_sector) != 0) {
            KernelFree(temp_sector);
            if (current_entry != root_entry) KernelFree(current_entry);
            KernelFree(pvd);
            return -1;
        }

        uint32_t offset_in_sector = bytes_read % ISO9660_SECTOR_SIZE;
        uint32_t remaining_in_sector = ISO9660_SECTOR_SIZE - offset_in_sector;
        uint32_t remaining_to_read = to_read - bytes_read;
        uint32_t chunk_size = (remaining_in_sector < remaining_to_read) ? remaining_in_sector : remaining_to_read;

        FastMemcpy(read_buffer + bytes_read, temp_sector + offset_in_sector, chunk_size);
        bytes_read += chunk_size;
        KernelFree(temp_sector);
    }

    if (current_entry != root_entry) {
        KernelFree(current_entry);
    }
    KernelFree(pvd);

    return bytes_read;
}

// This is a simplified version that just prints the directory contents.
// A more complete implementation would return a list of entries.
static Iso9660DirEntry** Iso9660ListDir(const char* path) {
    uint8_t* sector_buffer = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
    if (!sector_buffer) return NULL;

    Iso9660Pvd* pvd = NULL;
    for (uint32_t lba = 16; lba < 32; lba++) {
        if (ReadSector(lba, sector_buffer) != 0) {
            KernelFree(sector_buffer);
            return NULL;
        }
        if (FastMemcmp(((Iso9660Pvd*)sector_buffer)->id, "CD001", 5) == 0 && ((Iso9660Pvd*)sector_buffer)->type == 1) {
            pvd = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
            if (!pvd) {
                KernelFree(sector_buffer);
                return NULL;
            }
            FastMemcpy(pvd, sector_buffer, ISO9660_SECTOR_SIZE);
            break;
        }
    }
    KernelFree(sector_buffer);
    if (!pvd) return NULL;

    Iso9660DirEntry* root_entry = (Iso9660DirEntry*)pvd->root_directory_record;
    char* path_copy = KernelMemoryAlloc(FastStrlen(path, 256) + 1);
    if (!path_copy) {
        KernelFree(pvd);
        return NULL;
    }
    FastStrCopy(path_copy, path, 256);

    char* current_part = path_copy;
    Iso9660DirEntry* current_entry = root_entry;
    uint32_t current_lba = root_entry->extent_loc_le;
    uint32_t current_size = root_entry->data_length_le;

    if (FastStrCmp(path, "/") != 0) {
        while (*current_part) {
            if (*current_part == '/') {
                current_part++;
                continue;
            }

            char* next_slash = FastStrChr(current_part, '/');
            if (next_slash) {
                *next_slash = 0;
            }

            Iso9660DirEntry* found_entry = FindFileInDir(current_lba, current_size, current_part);
            if (current_entry != root_entry) {
                KernelFree(current_entry);
            }
            current_entry = found_entry;

            if (!current_entry) {
                KernelFree(path_copy);
                KernelFree(pvd);
                return NULL; // Path not found
            }

            current_lba = current_entry->extent_loc_le;
            current_size = current_entry->data_length_le;

            if (next_slash) {
                current_part = next_slash + 1;
            } else {
                break;
            }
        }
    }

    KernelFree(path_copy);

    if (!current_entry) {
        KernelFree(pvd);
        return NULL;
    }

    uint32_t dir_lba = current_entry->extent_loc_le;
    uint32_t dir_size = current_entry->data_length_le;
    if (current_entry != root_entry) {
        KernelFree(current_entry);
    }

    Iso9660DirEntry** entries = KernelMemoryAlloc(sizeof(Iso9660DirEntry*) * 65);
    if (!entries) {
        KernelFree(pvd);
        return NULL;
    }
    for (int i = 0; i < 65; i++) entries[i] = NULL;

    uint8_t* dir_sector = KernelMemoryAlloc(ISO9660_SECTOR_SIZE);
    if (!dir_sector) {
        KernelFree(pvd);
        KernelFree(entries);
        return NULL;
    }

    uint32_t bytes_read = 0;
    int entry_count = 0;
    while (bytes_read < dir_size && entry_count < 64) {
        if (ReadSector(dir_lba + (bytes_read / ISO9660_SECTOR_SIZE), dir_sector) != 0) {
            break;
        }

        Iso9660DirEntry* entry = (Iso9660DirEntry*)dir_sector;
        uint32_t sector_offset = 0;
        while (sector_offset < ISO9660_SECTOR_SIZE) {
            if (entry->length == 0) {
                break;
            }
            // Skip . and .. entries
            if (entry->file_id_length == 1 && (entry->file_id[0] == 0 || entry->file_id[0] == 1)) {
                sector_offset += entry->length;
                entry = (Iso9660DirEntry*)((uint8_t*)entry + entry->length);
                continue;
            }

            Iso9660DirEntry* new_entry = KernelMemoryAlloc(entry->length);
            if (new_entry) {
                FastMemcpy(new_entry, entry, entry->length);
                entries[entry_count++] = new_entry;
            }
            sector_offset += entry->length;
            entry = (Iso9660DirEntry*)((uint8_t*)entry + entry->length);
        }
        bytes_read += ISO9660_SECTOR_SIZE;
    }

    KernelFree(dir_sector);
    KernelFree(pvd);
    return entries;
}



int Iso9660CopyFile(const char* iso_path, const char* vfs_path) {
    int file_size = Iso9660Read(iso_path, NULL, 0);
    if (file_size < 0) {
        // Propagate read error
        PrintKernelError("Propagate read error\n");
        return -1;
    }
    if (file_size == 0) {
        // Empty file: create without allocating
        return VfsCreateFile(vfs_path);
    }
    void* buffer = KernelMemoryAlloc(file_size);
    if (!buffer) {
        PrintKernelError("Out of memory\n");
        return -1; // Out of memory
    }
    int bytes_read = Iso9660Read(iso_path, buffer, file_size);
    if (bytes_read <= 0) {
        KernelFree(buffer);
        PrintKernelError("Failed to read from ISO\n");
        return -1; // Failed to read from ISO
    }

    int bytes_written = VfsWriteFile(vfs_path, buffer, bytes_read);
    KernelFree(buffer);

    if (bytes_written <= 0) {
        PrintKernelError("Failed to write to FS\n");
        return -1; // Failed to write to VFS
    }

    return 0; // Success
}



int Iso9660Copy(const char* iso_path, const char* vfs_path) {
    Iso9660DirEntry** entries = Iso9660ListDir(iso_path);
    if (!entries) {
        // If listing directory fails, it might be a file.
        return Iso9660CopyFile(iso_path, vfs_path);
    }
    VfsCreateDir(vfs_path);

    for (int i = 0; entries[i] != NULL; i++) {
        char filename[256];
        FastMemcpy(filename, entries[i]->file_id, entries[i]->file_id_length);
        filename[entries[i]->file_id_length] = 0;

        char* semicolon = FastStrChr(filename, ';');
        if (semicolon) {
            *semicolon = 0;
        }

        char vfs_filepath[256];
        char iso_filepath[256];

        FormatA(vfs_filepath, sizeof(vfs_filepath), "%s/%s", vfs_path, filename);
        FormatA(iso_filepath, sizeof(iso_filepath), "%s/%s", iso_path, filename);

        if (entries[i]->file_flags & 2) { // Directory
            Iso9660Copy(iso_filepath, vfs_filepath);
        } else { // File
            Iso9660CopyFile(iso_filepath, vfs_filepath);
        }
        KernelFree(entries[i]);
    }

    KernelFree(entries);
    return 0;
}