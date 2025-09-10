#include "PEloader.h"
#include "../../mm/KernelHeap.h"
#include "../../mm/MemOps.h"
#include "../../mm/VMem.h"
#include "Console.h"
#include "Panic.h"
#include "MLFQ.h"
#include "VFS.h"

#define MAX_PE_FILE_SIZE (4 * 1024 * 1024)
#define DEFAULT_PROCESS_MEMORY_LIMIT (16 * 1024 * 1024)

static int ValidatePEHeader(const uint8_t* pe_data, uint64_t file_size) {
    if (file_size < sizeof(DOSHeader)) {
        PrintKernelError("PE: File too small for DOS header\n");
        return 0;
    }

    const DOSHeader* dos = (const DOSHeader*)pe_data;
    
    // Check DOS magic
    if (dos->e_magic != DOS_MAGIC) {
        PrintKernelError("PE: Invalid DOS magic\n");
        return 0;
    }

    // Check PE header offset bounds
    const uint64_t peoff = (uint64_t)dos->e_lfanew;
    const uint64_t need  = (uint64_t)sizeof(PEHeader) + (uint64_t)sizeof(OptionalHeader);
    if (peoff > file_size || peoff + need > file_size) {
        PrintKernelError("PE: PE header out of bounds\n");
        return 0;
    }

    const PEHeader* pe = (const PEHeader*)(pe_data + dos->e_lfanew);
    
    // Check PE magic
    if (pe->signature != PE_MAGIC) {
        PrintKernelError("PE: Invalid PE signature\n");
        return 0;
    }

    // Check machine type
    if (pe->machine != IMAGE_FILE_MACHINE_AMD64) {
        PrintKernelError("PE: Only x86-64 supported\n");
        return 0;
    }

    // Check if executable
    if (!(pe->characteristics & IMAGE_FILE_EXECUTABLE_IMAGE)) {
        PrintKernelError("PE: Not an executable file\n");
        return 0;
    }

    // Check optional header
    if (pe->opt_hdr_size < sizeof(OptionalHeader)) {
        PrintKernelError("PE: Invalid optional header size\n");
        return 0;
    }

    const OptionalHeader* opt = (const OptionalHeader*)(pe + 1);
    
    // Check PE32+ magic
    if (opt->magic != PE32PLUS_MAGIC) {
        PrintKernelError("PE: Only PE32+ supported\n");
        return 0;
    }

    // Validate sections bounds
    uint64_t sections_offset = dos->e_lfanew + sizeof(PEHeader) + pe->opt_hdr_size;
    if (sections_offset + (pe->sections * sizeof(SectionHeader)) > file_size) {
        PrintKernelError("PE: Section headers out of bounds\n");
        return 0;
    }

    return 1;
}

int ValidatePEFile(const uint8_t* pe_data, uint64_t size) {
    if (!pe_data || size < sizeof(DOSHeader)) {
        return 0;
    }
    return ValidatePEHeader(pe_data, size);
}

static uint64_t CalculatePEMemorySize(const uint8_t* pe_data) {
    const DOSHeader* dos = (const DOSHeader*)pe_data;
    const PEHeader* pe = (const PEHeader*)(pe_data + dos->e_lfanew);
    const OptionalHeader* opt = (const OptionalHeader*)(pe + 1);
    
    return opt->image_size;
}

uint32_t CreateProcessFromPE(const char* filename, const PELoadOptions* options) {
    if (!filename) {
        PrintKernelError("PE: NULL filename\n");
        return 0;
    }

    // Default options
    PELoadOptions default_opts = {
        .privilege_level = PROC_PRIV_USER,
        .security_flags = 0,
        .max_memory = DEFAULT_PROCESS_MEMORY_LIMIT,
        .process_name = filename
    };

    if (!options) {
        options = &default_opts;
    }

    // Security check
    MLFQProcessControlBlock* creator = MLFQGetCurrentProcess();
    if (!creator) {
        PrintKernelError("PE: No current process\n");
        return 0;
    }
    if (options->privilege_level == PROC_PRIV_SYSTEM &&
        creator->privilege_level != PROC_PRIV_SYSTEM) {
        PrintKernelError("PE: Unauthorized attempt to create system process\n");
        return 0;
    }

    PrintKernelSuccess("PE: Loading executable: ");
    PrintKernel(filename);
    PrintKernel("\n");

    // Get file size
    uint64_t file_size = VfsGetFileSize(filename);
    if (file_size == 0 || file_size > MAX_PE_FILE_SIZE) {
        PrintKernelError("PE: Invalid file size\n");
        return 0;
    }

    // Allocate and read file
    uint8_t* pe_data = (uint8_t*)VMemAllocWithGuards(file_size);
    if (!pe_data) {
        PrintKernelError("PE: Memory allocation failed\n");
        return 0;
    }

    int bytes_read = VfsReadFile(filename, (char*)pe_data, file_size);
    if (bytes_read != (int)file_size) {
        PrintKernelError("PE: File read failed\n");
        VMemFreeWithGuards(pe_data, file_size);
        return 0;
    }

    // Validate PE file
    if (!ValidatePEFile(pe_data, file_size)) {
        PrintKernelError("PE: Validation failed\n");
        VMemFreeWithGuards(pe_data, file_size);
        return 0;
    }

    // Get headers
    const DOSHeader* dos = (const DOSHeader*)pe_data;
    const PEHeader* pe = (const PEHeader*)(pe_data + dos->e_lfanew);
    const OptionalHeader* opt = (const OptionalHeader*)(pe + 1);

    // Calculate memory requirements
    uint64_t image_size = CalculatePEMemorySize(pe_data);
    if (image_size > options->max_memory) {
        PrintKernelError("PE: Image too large\n");
        VMemFreeWithGuards(pe_data, file_size);
        return 0;
    }

    // Allocate process memory
    void* process_memory = VMemAllocWithGuards(image_size);
    if (!process_memory) {
        PrintKernelError("PE: Process memory allocation failed\n");
        VMemFreeWithGuards(pe_data, file_size);
        return 0;
    }

    FastMemset(process_memory, 0, image_size);

    // Load sections
    const SectionHeader* sections = (const SectionHeader*)((uint8_t*)pe + sizeof(PEHeader) + pe->opt_hdr_size);
    
    for (int i = 0; i < pe->sections; i++) {
        const SectionHeader* sect = &sections[i];
        
        // Skip empty sections
        if (sect->raw_data_size == 0) continue;
        
        // Validate section bounds
        if (sect->raw_data_ptr + sect->raw_data_size > file_size ||
            sect->virtual_addr + sect->virtual_size > image_size) {
            PrintKernelError("PE: Section out of bounds\n");
            VMemFreeWithGuards(process_memory, image_size);
            VMemFreeWithGuards(pe_data, file_size);
            return 0;
        }

        // Copy section data
        FastMemcpy((uint8_t*)process_memory + sect->virtual_addr,
                   pe_data + sect->raw_data_ptr,
                   sect->raw_data_size);

        PrintKernelSuccess("PE: Loaded section ");
        PrintKernelInt(i);
        PrintKernel("\n");
    }

    // Calculate entry point
    if (opt->entry_point >= image_size) {
        PrintKernelError("PE: Entry point outside image\n");
        VMemFreeWithGuards(process_memory, image_size);
        VMemFreeWithGuards(pe_data, file_size);
        return 0;
    }
    void* entry_point = (uint8_t*)process_memory + opt->entry_point;

    // Create process
    uint32_t pid = MLFQCreateProcess(filename, (void (*)(void))entry_point);

    // Cleanup
    VMemFreeWithGuards(pe_data, file_size);

    if (pid == 0) {
        PrintKernelError("PE: Process creation failed\n");
        VMemFreeWithGuards(process_memory, image_size);
        return 0;
    }

    PrintKernelSuccess("PE: Process created with PID ");
    PrintKernelInt(pid);
    PrintKernel("\n");

    return pid;
}