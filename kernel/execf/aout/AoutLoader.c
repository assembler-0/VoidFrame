#include "AoutLoader.h"
#include "../../mm/VMem.h"
#include "../../mm/MemOps.h"
#include "Console.h"
#include "Scheduler.h"
#include "VFS.h"

#define MAX_AOUT_FILE_SIZE (2 * 1024 * 1024)
#define DEFAULT_PROCESS_MEMORY_LIMIT (8 * 1024 * 1024)

int ValidateAoutFile(const uint8_t* aout_data, uint64_t size) {
    if (!aout_data || size < sizeof(AoutHeader)) {
        return 0;
    }

    const AoutHeader* hdr = (const AoutHeader*)aout_data;
    
    // Check magic number
    if (hdr->a_magic != OMAGIC && hdr->a_magic != NMAGIC && 
        hdr->a_magic != ZMAGIC && hdr->a_magic != QMAGIC) {
        PrintKernelError("AOUT: Invalid magic number\n");
        return 0;
    }

    // Check if file contains all segments
    uint64_t required_size = sizeof(AoutHeader) + hdr->a_text + hdr->a_data + hdr->a_syms;
    if (size < required_size) {
        PrintKernelError("AOUT: File too small for segments\n");
        return 0;
    }

    // Sanity checks
    if (hdr->a_text > (4 * 1024 * 1024) || hdr->a_data > (4 * 1024 * 1024) || 
        hdr->a_bss > (8 * 1024 * 1024)) {
        PrintKernelError("AOUT: Segments too large\n");
        return 0;
    }

    return 1;
}

uint32_t CreateProcessFromAout(const char* filename, const AoutLoadOptions* options) {
    if (!filename) {
        PrintKernelError("AOUT: NULL filename\n");
        return 0;
    }

    // Default options
    AoutLoadOptions default_opts = {
        .privilege_level = PROC_PRIV_NORM,
        .security_flags = 0,
        .max_memory = DEFAULT_PROCESS_MEMORY_LIMIT,
        .process_name = filename
    };

    if (!options) {
        options = &default_opts;
    }

    PrintKernelSuccess("AOUT: Loading executable: ");
    PrintKernel(filename);
    PrintKernel("\n");

    // Get file size
    uint64_t file_size = VfsGetFileSize(filename);
    if (file_size == 0 || file_size > MAX_AOUT_FILE_SIZE) {
        PrintKernelError("AOUT: Invalid file size\n");
        return 0;
    }

    // Read file
    uint8_t* aout_data = (uint8_t*)VMemAllocWithGuards(file_size);
    if (!aout_data) {
        PrintKernelError("AOUT: Memory allocation failed\n");
        return 0;
    }

    int bytes_read = VfsReadFile(filename, (char*)aout_data, file_size);
    if (bytes_read != (int)file_size) {
        PrintKernelError("AOUT: File read failed\n");
        VMemFreeWithGuards(aout_data, file_size);
        return 0;
    }

    // Validate
    if (!ValidateAoutFile(aout_data, file_size)) {
        PrintKernelError("AOUT: Validation failed\n");
        VMemFreeWithGuards(aout_data, file_size);
        return 0;
    }

    const AoutHeader* hdr = (const AoutHeader*)aout_data;

    // Calculate total memory needed (text + data + bss)
    uint64_t total_memory = hdr->a_text + hdr->a_data + hdr->a_bss;
    if (total_memory > options->max_memory) {
        PrintKernelError("AOUT: Process too large\n");
        VMemFreeWithGuards(aout_data, file_size);
        return 0;
    }

    // Allocate process memory
    void* process_memory = VMemAllocWithGuards(total_memory);
    if (!process_memory) {
        PrintKernelError("AOUT: Process memory allocation failed\n");
        VMemFreeWithGuards(aout_data, file_size);
        return 0;
    }

    FastMemset(process_memory, 0, total_memory);

    // Load segments (this is the beauty of a.out - dead simple!)
    uint8_t* text_src = aout_data + sizeof(AoutHeader);
    uint8_t* data_src = text_src + hdr->a_text;

    // Copy text segment
    if (hdr->a_text > 0) {
        FastMemcpy(process_memory, text_src, hdr->a_text);
        PrintKernelSuccess("AOUT: Loaded text (");
        PrintKernelInt(hdr->a_text);
        PrintKernel(" bytes)\n");
    }

    // Copy data segment
    if (hdr->a_data > 0) {
        FastMemcpy((uint8_t*)process_memory + hdr->a_text, data_src, hdr->a_data);
        PrintKernelSuccess("AOUT: Loaded data (");
        PrintKernelInt(hdr->a_data);
        PrintKernel(" bytes)\n");
    }

    // BSS is already zeroed by FastMemset above
    if (hdr->a_bss > 0) {
        PrintKernelSuccess("AOUT: BSS segment (");
        PrintKernelInt(hdr->a_bss);
        PrintKernel(" bytes)\n");
    }

    // Entry point is absolute in a.out, but we need to adjust for our memory layout
    void* entry_point = (uint8_t*)process_memory + hdr->a_entry;

    // Create process
    uint32_t pid = CreateProcess(filename, (void (*)(void))entry_point);

    // Cleanup
    VMemFreeWithGuards(aout_data, file_size);

    if (pid == 0) {
        PrintKernelError("AOUT: Process creation failed\n");
        VMemFreeWithGuards(process_memory, total_memory);
        return 0;
    }

    PrintKernelSuccess("AOUT: Process created with PID ");
    PrintKernelInt(pid);
    PrintKernel("\n");

    return pid;
}