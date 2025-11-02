#include <ExecLoader.h>
#include <Console.h>
#include <MLFQ.h>
#include <VFS.h>
#include <aout/AoutLoader.h>
#include <elf/ELFloader.h>
#include <macho/Macho.h>
#include <macho/MachoLoader.h>
#include <pe/PEloader.h>

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

    // Check Mach-O magic
    if (size >= 4 && *(uint32_t*)data == MH_MAGIC_64) {
        return EXEC_FORMAT_MACHO64;
    }

    // Check a.out magic
    if (size >= 4) {
        uint32_t magic = *(uint32_t*)data;
        if (magic == 0407 || magic == 0410 || magic == 0413 || magic == 0314) {
            return EXEC_FORMAT_AOUT;
        }
    }

    return EXEC_FORMAT_UNKNOWN;
}

uint32_t LoadExecutable(const char* filename, const ExecLoadOptions* options) {
    if (!filename) {
        PrintKernelError("EXEC: NULL filename\n");
        return 0;
    }
    // Universal privilege enforcement
    if (options && options->privilege_level == PROC_PRIV_SYSTEM) {
        MLFQProcessControlBlock* creator = MLFQGetCurrentProcess();
        if (!creator || creator->privilege_level != PROC_PRIV_SYSTEM) {
            PrintKernelError("EXEC: Unauthorized privilege request\n");
            return 0;
        }
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
                .privilege_level = options ? options->privilege_level : PROC_PRIV_NORM,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (16 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromElf(filename, &elf_opts);
        }
        
        case EXEC_FORMAT_PE32PLUS: {
            PELoadOptions pe_opts = {
                .privilege_level = options ? options->privilege_level : PROC_PRIV_NORM,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (16 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromPE(filename, &pe_opts);
        }
        
        case EXEC_FORMAT_AOUT: {
            AoutLoadOptions aout_opts = {
                .privilege_level = options ? options->privilege_level : PROC_PRIV_NORM,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (8 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromAout(filename, &aout_opts);
        }

        case EXEC_FORMAT_MACHO64: {
            MachoLoadOptions macho_opts = {
                .privilege_level = options ? options->privilege_level : PROC_PRIV_NORM,
                .security_flags = options ? options->security_flags : 0,
                .max_memory = options ? options->max_memory : (16 * 1024 * 1024),
                .process_name = options ? options->process_name : filename
            };
            return CreateProcessFromMacho(filename, &macho_opts);
        }
        
        default:
            PrintKernelError("EXEC: Unknown executable format\n");
            return 0;
    }
}