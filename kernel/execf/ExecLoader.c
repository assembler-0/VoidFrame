#include "ExecLoader.h"
#include "Console.h"
#include "MLFQ.h"
#include "VFS.h"
#include "elf/ELFloader.h"
#include "pe/PEloader.h"

ExecFormat DetectExecutableFormat(const uint8_t* data, uint64_t size) {
    if (!data || size < 4) {
        return EXEC_FORMAT_UNKNOWN;
    }

    // Check ELF magic
    if (size >= 4 && data[0] == 0x7F && data[1] == 'E' && 
        data[2] == 'L' && data[3] == 'F') {
        return EXEC_FORMAT_ELF64;
    }

    // Check PE magic (DOS header)
    if (size >= 2 && data[0] == 'M' && data[1] == 'Z') {
        return EXEC_FORMAT_PE32PLUS;
    }

    return EXEC_FORMAT_UNKNOWN;
}

uint32_t LoadExecutable(const char* filename, const ExecLoadOptions* options) {
    if (!filename) {
        PrintKernelError("EXEC: NULL filename\n");
        return 0;
    }

    // Read first few bytes to detect format
    uint8_t header[64];
    int bytes_read = VfsReadFile(filename, (char*)header, sizeof(header));
    if (bytes_read < 4) {
        PrintKernelError("EXEC: Cannot read file header\n");
        return 0;
    }

    ExecFormat format = DetectExecutableFormat(header, bytes_read);
    
    switch (format) {
        case EXEC_FORMAT_ELF64: {
            ElfLoadOptions elf_opts = {
                .privilege_level = options ? options->privilege_level : PROC_PRIV_USER,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (16 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromElf(filename, &elf_opts);
        }
        
        case EXEC_FORMAT_PE32PLUS: {
            PELoadOptions pe_opts = {
                .privilege_level = options ? options->privilege_level : PROC_PRIV_USER,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (16 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromPE(filename, &pe_opts);
        }
        
        default:
            PrintKernelError("EXEC: Unknown executable format\n");
            return 0;
    }
}