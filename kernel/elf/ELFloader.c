#include "ELFloader.h"
#include "Console.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Process.h"
#include "VFS.h"

void* CreateProcessFromElf(const char* filename) {
    // 1. Read ELF file from VFS
    uint8_t* elf_data = (uint8_t*)KernelMemoryAlloc(65536);
    if (!elf_data) {
        PrintKernel("Failed to allocate memory for ELF data\n");
        return NULL;
    }

    int bytes_read = VfsReadFile(filename, (char*)elf_data, 65536);

    if (bytes_read <= 0) {
        PrintKernel("Failed to read ELF file\n");
        KernelFree(elf_data);
        return NULL;
    }
    PrintKernel("1 - ELF file read successfully\n");

    // 2. Validate ELF header with proper bounds checking
    if (sizeof(ElfHeader) > 65536) {
        PrintKernel("ELF header too large");
        KernelFree(elf_data);
        return NULL;
    }

    ElfHeader* header = (ElfHeader*)elf_data;

    // Check magic bytes properly (ELF magic is 0x7F454C46 in little endian)
    if (header->e_ident[0] != 0x7F ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F') {
        PrintKernel("Invalid ELF magic\n");
        KernelFree(elf_data);
        return NULL;
    }

    // Check architecture
    if (header->e_machine != EM_X86_64) {
        PrintKernel("Unsupported architecture\n");
        KernelFree(elf_data);
        return NULL;
    }

    // Check if program header table is within bounds
    if (header->e_phoff + (header->e_phnum * header->e_phentsize) > 65536) {
        PrintKernel("Program header table out of bounds\n");
        KernelFree(elf_data);
        return NULL;
    }

    PrintKernel("2 - ELF header validated\n");

    // 3. Find PT_LOAD segments and allocate memory
    ProgramHeader* ph = (ProgramHeader*)(elf_data + header->e_phoff);
    void* process_memory = NULL;
    uint64_t entry_point = header->e_entry;
    uint64_t base_vaddr = 0;
    uint64_t total_size = 0;

    // First pass: calculate total memory needed
    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            if (base_vaddr == 0) {
                base_vaddr = ph[i].p_vaddr;
            }
            uint64_t segment_end = ph[i].p_vaddr + ph[i].p_memsz - base_vaddr;
            if (segment_end > total_size) {
                total_size = segment_end;
            }
        }
    }

    if (total_size == 0) {
        PrintKernel("No loadable segments found\n");
        KernelFree(elf_data);
        return NULL;
    }

    // Allocate contiguous memory for entire process image
    process_memory = KernelMemoryAlloc(total_size);
    if (!process_memory) {
        PrintKernel("Failed to allocate process memory\n");
        KernelFree(elf_data);
        return NULL;
    }

    // Initialize all memory to zero
    FastMemset(process_memory, 0, total_size);

    // Second pass: load segments
    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            // Validate segment bounds
            if (ph[i].p_offset + ph[i].p_filesz > 65536) {
                PrintKernel("Segment data out of ELF bounds\n");
                KernelFree(process_memory);
                KernelFree(elf_data);
                return NULL;
            }

            // Calculate offset within allocated memory
            uint64_t mem_offset = ph[i].p_vaddr - base_vaddr;

            if (mem_offset + ph[i].p_memsz > total_size) {
                PrintKernel("Segment exceeds allocated memory\n");
                KernelFree(process_memory);
                KernelFree(elf_data);
                return NULL;
            }

            // Copy file data to memory
            if (ph[i].p_filesz > 0) {
                FastMemcpy((uint8_t*)process_memory + mem_offset,
                          elf_data + ph[i].p_offset,
                          ph[i].p_filesz);
            }

            // BSS section is already zeroed from initial memset
            PrintKernel("Loaded segment\n");
        }
    }

    PrintKernel("3 - All segments loaded\n");

    // 4. Create kernel process
    // Adjust entry point to be relative to our allocated memory
    void* adjusted_entry = (uint8_t*)process_memory + (entry_point - base_vaddr);

    uint32_t pid = CreateProcess((void (*)(void))adjusted_entry);
    KernelFree(elf_data);

    if (pid == 0) {
        PrintKernel("Failed to create process\n");
        KernelFree(process_memory);
        return NULL;
    }

    PrintKernel("4 - Process created successfully\n");
    return (void*)(uintptr_t)pid;
}

// Convenience function
int LoadElfFromFile(const char* filename) {
    void* process = CreateProcessFromElf(filename);
    return process ? 0 : -1;
}