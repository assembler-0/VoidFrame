#include "ELFloader.h"
#include "MemOps.h"
#include "VFS.h"
#include "Process.h"
#include "KernelHeap.h"

void* CreateProcessFromElf(const char* filename) {
    // 1. Read ELF file from VFS
    uint8_t* elf_data = (uint8_t*)KernelMemoryAlloc(65536); // Max 64KB for simple ELFs
    if (!VfsReadFile(filename, (char*)elf_data, 65536)) {
        KernelFree(elf_data);
        return NULL;
    }

    // 2. Validate ELF header
    ElfHeader* header = (ElfHeader*)elf_data;
    if (*(uint32_t*)header->e_ident != ELF_MAGIC ||
        header->e_machine != EM_X86_64) {
        KernelFree(elf_data);
        return NULL;
    }

    // 3. Find PT_LOAD segments and allocate memory
    ProgramHeader* ph = (ProgramHeader*)(elf_data + header->e_phoff);
    void* process_memory = NULL;
    uint64_t entry_point = header->e_entry;

    for (int i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            // Allocate memory for this segment
            void* segment_mem = KernelMemoryAlloc(ph[i].p_memsz);

            // Copy file data to memory
            FastMemcpy(segment_mem, elf_data + ph[i].p_offset, ph[i].p_filesz);

            // Zero out BSS section (if memsz > filesz)
            if (ph[i].p_memsz > ph[i].p_filesz) {
                memset((uint8_t*)segment_mem + ph[i].p_filesz, 0,
                       ph[i].p_memsz - ph[i].p_filesz);
            }

            // For simplicity, use first LOAD segment as process memory
            if (!process_memory) {
                process_memory = segment_mem;
            }
        }
    }

    // 4. Create kernel process
    if (process_memory) {
        // Create process with ELF entry point
        void* process = (void*)CreateProcess((void*)entry_point);
        KernelFree(elf_data);
        return process;
    }

    KernelFree(elf_data);
    return NULL;
}

// Convenience function
int LoadElfFromFile(const char* filename) {
    const void * process = CreateProcessFromElf(filename);
    return process ? 0 : -1;
}