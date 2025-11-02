#include <kernel/execf/macho/MachoLoader.h>
#include <kernel/execf/macho/Macho.h>
#include <mm/VMem.h>
#include <mm/MemOps.h>
#include <VFS.h>
#include <Console.h>
#include <Scheduler.h>

#define MAX_MACHO_FILE_SIZE (16 * 1024 * 1024) // 16MB limit for now

static int ValidateMachoHeader(const struct mach_header_64* header, uint64_t file_size) {
    if (header->magic != MH_MAGIC_64) {
        PrintKernelError("MACH-O: Invalid magic number\n");
        return 0;
    }
    // We can add more checks here later (cputype, filetype, etc.)
    if (header->sizeofcmds == 0 || header->ncmds == 0) {
        PrintKernelError("MACH-O: No load commands found\n");
        return 0;
    }
    if (header->sizeofcmds > file_size) {
        PrintKernelError("MACH-O: Invalid load commands size\n");
        return 0;
    }
    return 1;
}

static uint64_t CalculateProcessMemorySize(const struct mach_header_64* header, const uint8_t* macho_data) {
    uint64_t total_vmsize = 0;
    uint64_t current_offset = sizeof(struct mach_header_64);

    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command* lc = (const struct load_command*)(macho_data + current_offset);

        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)lc;
            total_vmsize += seg->vmsize;
        }

        current_offset += lc->cmdsize;
    }

    return total_vmsize;
}

uint32_t CreateProcessFromMacho(const char* filename, const MachoLoadOptions* options) {
    if (!filename) {
        PrintKernelError("MACH-O: NULL filename provided\n");
        return 0;
    }

    PrintKernelSuccess("MACH-O: Loading executable: ");
    PrintKernel(filename);
    PrintKernel("\n");

    // 1. Get file size
    uint64_t file_size = VfsGetFileSize(filename);
    if (file_size == 0 || file_size > MAX_MACHO_FILE_SIZE) {
        PrintKernelError("MACH-O: File is empty or too large.\n");
        return 0;
    }

    // 2. Allocate memory for the file content
    uint8_t* macho_data = (uint8_t*)VMemAllocWithGuards(file_size);
    if (!macho_data) {
        PrintKernelError("MACH-O: Could not allocate memory for file data.\n");
        return 0;
    }

    // 3. Read the file
    int bytes_read = VfsReadFile(filename, (char*)macho_data, file_size);
    if (bytes_read <= 0 || (uint64_t)bytes_read != file_size) {
        PrintKernelError("MACH-O: Failed to read file.\n");
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }

    // 4. Validate the header
    const struct mach_header_64* header = (const struct mach_header_64*)macho_data;
    if (!ValidateMachoHeader(header, file_size)) {
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }

    PrintKernelSuccess("MACH-O: Header validation passed.\n");

    // 5. Calculate memory size by iterating load commands
    uint64_t process_memory_size = CalculateProcessMemorySize(header, macho_data);
    if (process_memory_size == 0) {
        PrintKernelError("MACH-O: No loadable segments found or memory size is zero.\n");
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }

    PrintKernelSuccess("MACH-O: Calculated process memory size: ");
    PrintKernelInt((uint32_t)process_memory_size);
    PrintKernel(" bytes\n");

    // 6. Allocate process memory
    void* process_memory = VMemAllocWithGuards(process_memory_size);
    if (!process_memory) {
        PrintKernelError("MACH-O: Failed to allocate memory for the process.\n");
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }
    FastMemset(process_memory, 0, process_memory_size);

    // 7. Load segments
    uint64_t current_offset = sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command* lc = (const struct load_command*)(macho_data + current_offset);

        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)lc;

            if (seg->fileoff + seg->filesize > file_size) {
                PrintKernelError("MACH-O: Segment data out of file bounds.\n");
                VMemFreeWithGuards(process_memory, process_memory_size);
                VMemFreeWithGuards(macho_data, file_size);
                return 0;
            }

            if (seg->vmaddr + seg->vmsize > process_memory_size) {
                PrintKernelError("MACH-O: Segment would overflow process memory.\n");
                VMemFreeWithGuards(process_memory, process_memory_size);
                VMemFreeWithGuards(macho_data, file_size);
                return 0;
            }

            // Copy data from file
            if (seg->filesize > 0) {
                FastMemcpy((uint8_t*)process_memory + seg->vmaddr, macho_data + seg->fileoff, seg->filesize);
            }

            // Zero out BSS
            if (seg->vmsize > seg->filesize) {
                FastMemset((uint8_t*)process_memory + seg->vmaddr + seg->filesize, 0, seg->vmsize - seg->filesize);
            }

            PrintKernelSuccess("MACH-O: Loaded segment ");
            PrintKernel(seg->segname);
            PrintKernel("\n");
        }

        current_offset += lc->cmdsize;
    }

    // 8. Find entry point
    uint64_t entry_point_offset = 0;
    uint64_t stack_size = 0;
    current_offset = sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command* lc = (const struct load_command*)(macho_data + current_offset);
        if (lc->cmd == LC_MAIN) {
            const struct entry_point_command* main_cmd = (const struct entry_point_command*)lc;
            entry_point_offset = main_cmd->entryoff;
            stack_size = main_cmd->stacksize;
            break;
        }
        current_offset += lc->cmdsize;
    }

    if (entry_point_offset == 0) {
        PrintKernelError("MACH-O: Could not find entry point (LC_MAIN not found).\n");
        VMemFreeWithGuards(process_memory, process_memory_size);
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }

    // Find the segment containing the entry point and calculate the virtual address
    uint64_t entry_point_va = 0;
    current_offset = sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const struct load_command* lc = (const struct load_command*)(macho_data + current_offset);
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)lc;
            if (entry_point_offset >= seg->fileoff && entry_point_offset < (seg->fileoff + seg->filesize)) {
                entry_point_va = seg->vmaddr + (entry_point_offset - seg->fileoff);
                PrintKernelSuccess("MACH-O: Entry point found in segment: ");
                PrintKernel(seg->segname);
                PrintKernel(" at VA: ");
                PrintKernelHex((uint32_t)entry_point_va);
                PrintKernel("\n");
                break;
            }
        }
        current_offset += lc->cmdsize;
    }

    if (entry_point_va == 0) {
        PrintKernelError("MACH-O: Entry point offset does not map to any segment.\n");
        VMemFreeWithGuards(process_memory, process_memory_size);
        VMemFreeWithGuards(macho_data, file_size);
        return 0;
    }

    // The entry point is relative to the process memory base
    void* adjusted_entry = (uint8_t*)process_memory + entry_point_va;

    // 9. Create process
    uint32_t pid = CreateProcess(filename, (void (*)(void))adjusted_entry);

    // Clean up temporary Mach-O data
    VMemFreeWithGuards(macho_data, file_size);

    if (pid == 0) {
        PrintKernelError("MACH-O: Failed to create process\n");
        VMemFreeWithGuards(process_memory, process_memory_size);
        return 0;
    }

    PrintKernelSuccess("MACH-O: Process created successfully (PID: ");
    PrintKernelInt(pid);
    PrintKernel(")\n");

    return pid;
}