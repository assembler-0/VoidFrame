#ifndef VOIDFRAME_MACHO_H
#define VOIDFRAME_MACHO_H

#include <stdint.h>

typedef int32_t cpu_type_t;
typedef int32_t cpu_subtype_t;
typedef int32_t vm_prot_t;

#define MH_MAGIC_64 0xfeedfacf /* the 64-bit mach magic number */
#define MH_CIGAM_64 0xcffaedfe /* NXSwapInt(MH_MAGIC_64) */

struct mach_header_64 {
    uint32_t    magic;        /* mach magic number identifier */
    cpu_type_t  cputype;      /* cpu specifier */
    cpu_subtype_t cpusubtype;   /* machine specifier */
    uint32_t    filetype;     /* type of file */
    uint32_t    ncmds;        /* number of load commands */
    uint32_t    sizeofcmds;   /* the size of all the load commands */
    uint32_t    flags;        /* flags */
    uint32_t    reserved;     /* reserved */
};

struct load_command {
    uint32_t cmd;      /* type of load command */
    uint32_t cmdsize;  /* total size of command in bytes */
};

/* Constants for the cmd field of all load commands, the type */
#define LC_SEGMENT_64 0x19 /* 64-bit segment of this file to be mapped */
#define LC_SYMTAB 0x2 /* link-edit stab symbol table info */
#define LC_UNIXTHREAD 0x5 /* unix thread head */
#define LC_LOAD_DYLIB 0xc /* load a dynamically linked shared library */
#define LC_DYLD_INFO_ONLY 0x80000022 /* compressed dyld information */
#define LC_MAIN 0x80000028 /* replacement for LC_UNIXTHREAD */


struct segment_command_64 { /* for 64-bit architectures */
    uint32_t    cmd;        /* LC_SEGMENT_64 */
    uint32_t    cmdsize;    /* includes sizeof section_64 structs */
    char        segname[16];/* segment name */
    uint64_t    vmaddr;     /* memory address of this segment */
    uint64_t    vmsize;     /* memory size of this segment */
    uint64_t    fileoff;    /* file offset of this segment */
    uint64_t    filesize;   /* amount to map from the file */
    vm_prot_t   maxprot;    /* maximum virtual memory protection */
    vm_prot_t   initprot;   /* initial virtual memory protection */
    uint32_t    nsects;     /* number of sections in this segment */
    uint32_t    flags;      /* flags */
};

struct section_64 { /* for 64-bit architectures */
    char        sectname[16];/* name of this section */
    char        segname[16];/* segment this section is in */
    uint64_t    addr;       /* memory address of this section */
    uint64_t    size;       /* size in bytes of this section */
    uint32_t    offset;     /* file offset of this section */
    uint32_t    align;      /* section alignment (power of 2) */
    uint32_t    reloff;     /* file offset of relocation entries */
    uint32_t    nreloc;     /* number of relocation entries */
    uint32_t    flags;      /* flags (section type and attributes)*/
    uint32_t    reserved1;  /* reserved (for offset or index) */
    uint32_t    reserved2;  /* reserved (for count or sizeof) */
    uint32_t    reserved3;  /* reserved */
};

struct entry_point_command {
    uint32_t  cmd;       /* LC_MAIN */
    uint32_t  cmdsize;   /* sizeof(struct entry_point_command) */
    uint64_t  entryoff;  /* file (__TEXT) offset of main() */
    uint64_t  stacksize; /* if not 0, initial stack size */
};

#endif //VOIDFRAME_MACHO_H
