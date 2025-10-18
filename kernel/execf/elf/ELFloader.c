#include "ELFloader.h"
#include "../../mm/KernelHeap.h"
#include "../../mm/MemOps.h"
#include "../../mm/VMem.h"
#include "Console.h"
#include "Scheduler.h"
#include "StackGuard.h"
#include "VFS.h"

// Default maximum ELF file size (4MB)
#define MAX_ELF_FILE_SIZE (4 * 1024 * 1024)

// Default sched memory limit (16MB)
#define DEFAULT_PROCESS_MEMORY_LIMIT (16 * 1024 * 1024)

static int ValidateElfHeader(const ElfHeader* header, uint64_t file_size) {
    // Check magic bytes
    if (header->e_ident[0] != 0x7F ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F') {
        PrintKernelError("ELF: Invalid magic bytes\n");
        return 0;
    }

    // Check class (64-bit)
    if (header->e_ident[4] != ELFCLASS64) {
        PrintKernelError("ELF: Only 64-bit ELF files supported\n");
        return 0;
    }

    // Check data encoding (little-endian)
    if (header->e_ident[5] != ELFDATA2LSB) {
        PrintKernelError("ELF: Only little-endian ELF files supported\n");
        return 0;
    }

    // Check file type (executable)
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        PrintKernelError("ELF: Only executable files supported\n");
        return 0;
    }

    // Check architecture
    if (header->e_machine != EM_X86_64) {
        PrintKernelError("ELF: Only x86-64 architecture supported\n");
        return 0;
    }

    // Validate program header table bounds
    if (header->e_phoff + (header->e_phnum * header->e_phentsize) > file_size) {
        PrintKernelError("ELF: Program header table out of bounds\n");
        return 0;
    }

    // Check for reasonable limits
    if (header->e_phnum > 64) {
        PrintKernelError("ELF: Too many program headers\n");
        return 0;
    }

    if (header->e_entry == 0) {
        PrintKernelError("ELF: Invalid entry point\n");
        return 0;
    }

    return 1;
}

int ValidateElfFile(const uint8_t* elf_data, uint64_t size) {
    if (!elf_data || size < sizeof(ElfHeader)) {
        return 0;
    }

    const ElfHeader* header = (const ElfHeader*)elf_data;
    return ValidateElfHeader(header, size);
}

static uint64_t CalculateProcessMemorySize(const ElfHeader* header, const uint8_t* elf_data) {
    const ProgramHeader* ph = (const ProgramHeader*)(elf_data + header->e_phoff);
    uint64_t lowest_addr = UINT64_MAX;
    uint64_t highest_addr = 0;

    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            if (ph[i].p_vaddr < lowest_addr) {
                lowest_addr = ph[i].p_vaddr;
            }
            uint64_t segment_end = ph[i].p_vaddr + ph[i].p_memsz;
            if (segment_end > highest_addr) {
                highest_addr = segment_end;
            }
        }
    }

    if (lowest_addr == UINT64_MAX) {
        return 0; // No loadable segments
    }

    return highest_addr - lowest_addr;
}

uint32_t CreateProcessFromElf(const char* filename, const ElfLoadOptions* options) {
    if (!filename) {
        PrintKernelError("ELF: NULL filename provided\n");
        return 0;
    }

    // Set default options if none provided
    ElfLoadOptions default_opts = {
        .privilege_level = PROC_PRIV_NORM,
        .security_flags = 0,
        .max_memory = DEFAULT_PROCESS_MEMORY_LIMIT,
        .process_name = filename
    };

    if (!options) {
        options = &default_opts;
    }

    // Security check: Only system processes can create system processes
    CurrentProcessControlBlock* creator = GetCurrentProcess();
    if (!creator) {
        PrintKernelError("ELF: No current process\n");
        return 0;
    }
    if (options->privilege_level == PROC_PRIV_SYSTEM &&
        creator->privilege_level != PROC_PRIV_SYSTEM) {
        PrintKernelError("ELF: Unauthorized attempt to create system sched\n");
        return 0;
    }

    PrintKernelSuccess("ELF: Loading executable: ");
    PrintKernel(filename);
    PrintKernel("\n");

    // 1. Determine file size first
    uint64_t file_size = VfsGetFileSize(filename);
    if (file_size == 0 || file_size > MAX_ELF_FILE_SIZE) {
        PrintKernelError("ELF: File too large or empty (");
        PrintKernelInt((uint32_t)file_size);
        PrintKernel(" bytes)\n");
        return 0;
    }

    // 2. Allocate memory for ELF file with guards
    uint8_t* elf_data = (uint8_t*)VMemAllocWithGuards(file_size);
    if (!elf_data) {
        PrintKernelError("ELF: Failed to allocate memory for ELF data\n");
        return 0;
    }

    // 3. Read ELF file from VFS
    int bytes_read = VfsReadFile(filename, (char*)elf_data, file_size);
    if (bytes_read <= 0 || (uint64_t)bytes_read != file_size) {
        PrintKernelError("ELF: Failed to read file completely (or incomplete read)\n");
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    PrintKernelSuccess("ELF: File loaded (");
    PrintKernelInt((uint32_t)bytes_read);
    PrintKernel(" bytes)\n");

    // 4. Validate ELF header
    if (!ValidateElfFile(elf_data, file_size)) {
        PrintKernelError("ELF: File validation failed\n");
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    const ElfHeader* header = (const ElfHeader*)elf_data;
    PrintKernelSuccess("ELF: Header validation passed\n");

    // 5. Calculate required memory for sched
    uint64_t process_memory_size = CalculateProcessMemorySize(header, elf_data);
    if (process_memory_size == 0) {
        PrintKernelError("ELF: No loadable segments found\n");
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    if (process_memory_size > options->max_memory) {
        PrintKernelError("ELF: Process memory requirement (");
        PrintKernelInt((uint32_t)process_memory_size);
        PrintKernel(") exceeds limit (");
        PrintKernelInt((uint32_t)options->max_memory);
        PrintKernel(")\n");
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    // 6. Allocate protected memory for sched image
    void* process_memory = VMemAllocWithGuards(process_memory_size);
    if (!process_memory) {
        PrintKernelError("ELF: Failed to allocate sched memory\n");
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    // Initialize memory to zero
    FastMemset(process_memory, 0, process_memory_size);

    // 7. Load segments into memory
    const ProgramHeader* ph = (const ProgramHeader*)(elf_data + header->e_phoff);
    uint64_t base_vaddr = UINT64_MAX;

    // Find the base virtual address
    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr < base_vaddr) {
            base_vaddr = ph[i].p_vaddr;
        }
    }

    // Load each PT_LOAD segment
    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            // Validate segment bounds
            if (ph[i].p_offset + ph[i].p_filesz > file_size) {
                PrintKernelError("ELF: Segment data out of file bounds\n");
                VMemFreeWithGuards(process_memory, process_memory_size);
                VMemFreeWithGuards(elf_data, file_size);
                return 0;
            }

            // Calculate memory offset
            uint64_t mem_offset = ph[i].p_vaddr - base_vaddr;
            if (mem_offset + ph[i].p_memsz > process_memory_size) {
                PrintKernelError("ELF: Segment exceeds allocated memory\n");
                VMemFreeWithGuards(process_memory, process_memory_size);
                VMemFreeWithGuards(elf_data, file_size);
                return 0;
            }

            // Copy file data to memory
            if (ph[i].p_filesz > 0) {
                FastMemcpy((uint8_t*)process_memory + mem_offset,
                          elf_data + ph[i].p_offset,
                          ph[i].p_filesz);
            }

            // Zero out BSS if p_memsz > p_filesz
            if (ph[i].p_memsz > ph[i].p_filesz) {
                FastMemset((uint8_t*)process_memory + mem_offset + ph[i].p_filesz,
                          0,
                          ph[i].p_memsz - ph[i].p_filesz);
            }

            PrintKernelSuccess("ELF: Loaded segment ");
            PrintKernelInt(i);
            PrintKernel(" (");
            PrintKernelInt((uint32_t)ph[i].p_memsz);
            PrintKernel(" bytes)\n");
        }
    }

    // 8. Calculate adjusted entry point
    uint64_t entry_point = header->e_entry;
    if (entry_point < base_vaddr || entry_point >= base_vaddr + process_memory_size) {
        PrintKernelError("ELF: Entry point outside loaded segments\n");
        VMemFreeWithGuards(process_memory, process_memory_size);
        VMemFreeWithGuards(elf_data, file_size);
        return 0;
    }

    void* adjusted_entry = (uint8_t*)process_memory + (entry_point - base_vaddr);

    // 9. Create sched with enhanced security
    uint32_t pid = CreateProcess(filename, (void (*)(void))adjusted_entry);

    // Clean up temporary ELF data
    VMemFreeWithGuards(elf_data, file_size);

    if (pid == 0) {
        PrintKernelError("ELF: Failed to create sched\n");
        VMemFreeWithGuards(process_memory, process_memory_size);
        return 0;
    }

    PrintKernelSuccess("ELF: Process created successfully (PID: ");
    PrintKernelInt(pid);
    PrintKernel(")\n");

    // Check for resource leaks after ELF loading
    CheckResourceLeaks();

    return pid;
}

// Simple wrapper for backward compatibility
uint32_t LoadElfExecutable(const char* filename) {
    return CreateProcessFromElf(filename, NULL);
}

// Legacy compatibility function
int LoadElfFromFile(const char* filename) {
    uint32_t pid = LoadElfExecutable(filename);
    return pid ? 0 : -1;
}
