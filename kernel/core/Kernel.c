// VoidFrame Kernel Entry File
#include "Kernel.h"
#include "APIC.h"
#include "Compositor.h"
#include "Console.h"
#include "EXT/Ext2.h"
#include "FAT/FAT1x.h"
#include "Gdt.h"
#include "ISA.h"
#include "Ide.h"
#include "Idt.h"
#include "InitRD.h"
#include "Io.h"
#include "KernelHeap.h"
#include "LPT/LPT.h"
#include "MLFQ.h"
#include "MemOps.h"
#include "MemPool.h"
#include "Multiboot2.h"
#include "PCI/PCI.h"
#include "PMem.h"
#include "PS2.h"
#include "Panic.h"
#include "SVGAII.h"
#include "Serial.h"
#include "Shell.h"
#include "StackGuard.h"
#include "Switch.h"
#include "VFRFS.h"
#include "VFS.h"
#include "VMem.h"
#include "Vesa.h"
#include "ethernet/Network.h"
#include "sound/Generic.h"
#include "stdbool.h"
#include "stdint.h"
#include "storage/AHCI.h"
#include "xHCI/xHCI.h"

void KernelMainHigherHalf(void);
#define KERNEL_STACK_SIZE (32 * 1024) // 32KB stack
static uint8_t kernel_stack[KERNEL_STACK_SIZE]; // Statically allocate for simplicity
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

// Global variable to store the Multiboot2 info address
uint32_t g_multiboot_info_addr = 0;
bool g_svgaII_active = false;
bool g_HasKernelStarted = false;

void ParseMultibootInfo(uint32_t info) {
    g_multiboot_info_addr = info;
    PrintKernel("Info: Parsing Multiboot2 info...\n");
    uint32_t total_size = *(uint32_t*)info;
    PrintKernel("Multiboot2 total size: ");
    PrintKernelInt(total_size);
    PrintKernel("\n");

    // Start parsing tags after the total_size and reserved fields (8 bytes)
    struct MultibootTag* tag = (struct MultibootTag*)(uintptr_t)(info + 8);
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        PrintKernel("  Tag type: ");
        PrintKernelInt(tag->type);
        PrintKernel(", size: ");
        PrintKernelInt(tag->size);
        PrintKernel("\n");

        if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER) {
            PrintKernel("    Framebuffer Tag found!\n");
        } else if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            PrintKernel("    Memory Map Tag found\n");
        } else if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
            PrintKernel("    Module Tag found\n");
            struct MultibootModuleTag* mod_tag = (struct MultibootModuleTag*)tag;
            PrintKernel("      Start: 0x");
            PrintKernelHex(mod_tag->mod_start);
            PrintKernel(", End: 0x");
            PrintKernelHex(mod_tag->mod_end);
            PrintKernel("\n      Cmdline: ");
            PrintKernel(mod_tag->cmdline);
            PrintKernel("\n");
        }
        // Move to the next tag, ensuring 8-byte alignment
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
    PrintKernelSuccess("System: Multiboot2 info parsed.\n");
}

uint64_t AllocPageTable(const char* table_name) {
    (void)table_name;
    uint64_t table_phys = 0;
    for (int attempt = 0; attempt < 32; attempt++) {
        void* candidate = AllocPage();
        if (!candidate) {
            PANIC("Bootstrap: Out of memory allocating");
        }
        if ((uint64_t)candidate < IDENTITY_MAP_SIZE) {
            table_phys = (uint64_t)candidate;
            break;
        }
        FreePage(candidate);
    }
    if (!table_phys) {
        PANIC("Bootstrap: Failed to allocate in identity-mapped memory");
    }
    if (table_phys & 0xFFF) PANIC("Page table not aligned");
    FastZeroPage((void*)table_phys);
    return table_phys;
}

void BootstrapMapPage(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // Input validation
    if (!pml4_phys || (pml4_phys & 0xFFF)) PANIC("Invalid PML4 address");
    if (vaddr & 0xFFF || paddr & 0xFFF) {
        vaddr &= ~0xFFF;  // Page-align virtual address
        paddr &= ~0xFFF;  // Page-align physical address
    }

    uint64_t* pml4 = (uint64_t*)pml4_phys;

    // 1. Get/Create PDPT
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_phys;
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        pdpt_phys = AllocPageTable("PDPT");
        pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pdpt_phys = pml4[pml4_idx] & PT_ADDR_MASK;
        if (!pdpt_phys) PANIC("Corrupted PDPT entry");
    }

    // 2. Get/Create PD
    uint64_t* pdpt = (uint64_t*)pdpt_phys;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_phys;
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        pd_phys = AllocPageTable("PD");
        pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pd_phys = pdpt[pdpt_idx] & PT_ADDR_MASK;
    }

    // 3. Get/Create PT
    uint64_t* pd = (uint64_t*)pd_phys;
    const int pd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pt_phys;
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        pt_phys = AllocPageTable("PT");
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pt_phys = pd[pd_idx] & PT_ADDR_MASK;
    }

    // 4. Set the final PTE with validation
    uint64_t* pt = (uint64_t*)pt_phys;
    const int pt_idx = (vaddr >> 12) & 0x1FF;

    // NEW: Check for remapping
    if (pt[pt_idx] & PAGE_PRESENT) {
        uint64_t existing_paddr = pt[pt_idx] & PT_ADDR_MASK;
        if (existing_paddr != paddr) {
            PrintKernelWarning("[BOOTSTRAP] Remapping 0x");
            PrintKernelHex(vaddr);
            PrintKernel(" from 0x");
            PrintKernelHex(existing_paddr);
            PrintKernel(" to 0x");
            PrintKernelHex(paddr);
            PrintKernel("\n");
        }
    }

    pt[pt_idx] = paddr | flags | PAGE_PRESENT;

    static uint64_t pages_mapped = 0;
    pages_mapped++;

    // Show progress every 64K pages (256MB)
    if (pages_mapped % 65536 == 0) {
        PrintKernelInt((pages_mapped * PAGE_SIZE) / (1024 * 1024));
        PrintKernel("MB ");
    }
    // Show dots every 16K pages (64MB) for finer progress
    else if (pages_mapped % 16384 == 0) {
        PrintKernel(".");
    }
}

void CPUFeatureValidation(void) {
    uint32_t eax, ebx, ecx, edx;

    // Check for standard features (EAX=1)
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));

    bool has_sse = (edx & (1 << 25)) != 0;
    bool has_sse2 = (edx & (1 << 26)) != 0;
    bool has_sse3 = (ecx & (1 << 0)) != 0;
    bool has_ssse3 = (ecx & (1 << 9)) != 0;
    bool has_sse4_1 = (ecx & (1 << 19)) != 0;
    bool has_sse4_2 = (ecx & (1 << 20)) != 0;
    bool has_avx = (ecx & (1 << 28)) != 0;

    // Check for extended features (EAX=7, ECX=0)
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));


    bool has_avx2 = (ebx & (1 << 5)) != 0;
    bool has_bmi1 = (ebx & (1 << 3)) != 0;
    bool has_bmi2 = (ebx & (1 << 8)) != 0;
    // FMA (FMA3) is CPUID.(EAX=1):ECX[12]
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    bool has_fma = (ecx & (1 << 12)) != 0;

    if (!has_sse) {
        PrintKernelWarning("System: This kernel requires SSE support but the extension is not found. (CPUID)\n");
    }
    if (!has_sse2) {
        PrintKernelWarning("System: This kernel requires SSE2 support but the extension is not found. (CPUID)\n");
    }
    if (!has_sse3) {
        PrintKernelWarning("System: This kernel requires SSE3 support but the extension is not found. (CPUID)\n");
    }
    if (!has_ssse3) {
        PrintKernelWarning("System: This kernel requires SSSE3 support but the extension is not found. (CPUID)\n");
    }
    if (!has_sse4_1) {
        PrintKernelWarning("System: This kernel requires SSE4.1 support but the extension is not found. (CPUID)\n");
    }
    if (!has_sse4_2) {
        PrintKernelWarning("System: This kernel requires SSE4.2 support but the extension is not found. (CPUID)\n");
    }
    if (!has_avx) {
        PrintKernelWarning("System: This kernel requires AVX support but the extension is not found. (CPUID)\n");
    }
    if (!has_avx2) {
        PrintKernelWarning("System: This kernel requires AVX2 support (2013+ CPUs) but the extension is not found. (CPUID)\n");
    }
    if (!has_fma) {
        PrintKernelWarning("System: This kernel requires FMA3 support but the extension is not found. (CPUID)\n");
    }
    if (!has_bmi1) {
        PrintKernelWarning("System: This kernel requires BMI1 support but the extension is not found. (CPUID)\n");
    }
    if (!has_bmi2) {
        PrintKernelWarning("System: This kernel requires BMI2 support but the extension is not found. (CPUID)\n");
    }
}

// Memory hardening functions
static void SetupMemoryProtection(void) {
    PrintKernel("System: Setting up memory protection...\n");

    // Check CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    bool protection_enabled = false;

    // Enable SMEP if supported (bit 7 in EBX from CPUID leaf 7)
    if (ebx & (1 << 7)) {
        cr4 |= (1ULL << 20);  // CR4.SMEP
        PrintKernel("System: SMEP enabled\n");
        protection_enabled = true;
    }

    if (ecx & (1 << 7)) {
        PrintKernelSuccess("System: STAC/CLAC instructions are supported\n");
    }

    // Enable SMAP if supported (bit 20 in EBX from CPUID leaf 7)
    if (ebx & (1 << 20)) {
        cr4 |= (1ULL << 21);  // CR4.SMAP
        PrintKernel("System: SMAP enabled\n");
        protection_enabled = true;
    }

    // enable NX
    __asm__ volatile("cpuid" : "=a"(eax), "=d"(edx) : "a"(0x80000001) : "ebx", "ecx");
    if (edx & (1u << 20)) {                // CPUID.80000001H:EDX.NX[20]
        uint32_t efer_lo, efer_hi;
        // Read EFER (MSR 0xC0000080) into EDX:EAX
        __asm__ volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(0xC0000080));
        efer_lo |= (1u << 11);             // EFER.NXE is bit 11 (low 32 bits)
        // Write back both halves to EFER
        __asm__ volatile("wrmsr" :: "c"(0xC0000080), "a"(efer_lo), "d"(efer_hi));
        PrintKernel("System: NX bit enabled\n");
        protection_enabled = true;
    }

    // Enable PCID for faster context switches
    __asm__ volatile("cpuid" : "=a"(eax), "=c"(ecx) : "a"(1) : "ebx", "edx");
    if (ecx & (1 << 17)) {
        cr4 |= (1ULL << 17);  // CR4.PCIDE
        PrintKernel("System: PCID enabled\n");
        protection_enabled = true;
    }

    if (ecx & (1 << 2)) {
        cr4 |= (1ULL << 11);  // CR4.UMIP
        PrintKernel("System: UMIP enabled (blocks privileged instructions in usermode)\n");
        protection_enabled = true;
    }

    // Enable PKE (Protection Keys for Userspace) if available
    if (ecx & (1 << 3)) {
        cr4 |= (1ULL << 22);  // CR4.PKE
        PrintKernel("System: PKE enabled (memory protection keys)\n");
        protection_enabled = true;
    }

    // Enable CET (Control Flow Enforcement Technology) if available
    __asm__ volatile("cpuid" : "=a"(eax), "=c"(ecx) : "a"(7), "c"(0) : "ebx", "edx");
    if (ecx & (1 << 7)) {  // CET_SS (Shadow Stack)
        // Enable CET in CR4
        cr4 |= (1ULL << 23);  // CR4.CET

        // Configure CET MSRs
        uint32_t cet_u_lo = (1 << 0) | (1 << 1);  // SH_STK_EN | WR_SHSTK_EN
        __asm__ volatile("wrmsr" :: "c"(0x6A2), "a"(cet_u_lo), "d"(0));  // MSR_IA32_U_CET

        PrintKernel("System: CET Shadow Stack enabled\n");
        protection_enabled = true;
    }

    // Set up Write Protection (WP) bit - prevents kernel writes to read-only pages
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);  // CR0.WP
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    PrintKernel("System: Write Protection (WP) enabled\n");

    // Enable FSGSBASE for faster userspace context switches
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    if (ebx & (1 << 0)) {
        cr4 |= (1ULL << 16);  // CR4.FSGSBASE
        PrintKernel("System: FSGSBASE enabled\n");
        protection_enabled = true;
    }

    // Write back the modified CR4
    if (protection_enabled) {
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
        PrintKernelSuccess("System: Memory protection configured\n");
    } else {
        PrintKernel("System: No memory protection features available\n");
    }

}

static bool CheckHugePageSupport(void) {
    uint32_t eax, ebx, ecx, edx;

    // Check for PSE (Page Size Extension) - required for 2MB pages
    __asm__ volatile("cpuid" : "=a"(eax), "=d"(edx) : "a"(1) : "ebx", "ecx");
    if (!(edx & (1 << 3))) {
        PrintKernel("Info: PSE not supported - no huge pages\n");
        return false;
    }

    // Check for PSE-36 for extended physical addressing
    if (edx & (1 << 17)) {
        PrintKernel("Info: PSE-36 supported\n");
    }

    return true;
}

static void ValidateMemoryLayout(void) {
    PrintKernel("System: Validating memory layout...\n");

    const uint64_t kernel_start = (uint64_t)_kernel_phys_start;
    const uint64_t kernel_end = (uint64_t)_kernel_phys_end;
    const uint64_t kernel_size = kernel_end - kernel_start;

    PrintKernel("  Kernel: 0x");
    PrintKernelHex(kernel_start);
    PrintKernel(" - 0x");
    PrintKernelHex(kernel_end);
    PrintKernel(" (");
    PrintKernelInt(kernel_size / 1024);
    PrintKernel(" KB)\n");

    _Static_assert(VIRT_ADDR_SPACE_HIGH_START < VIRT_ADDR_SPACE_HIGH_END, "VIRT addr space invalid");
    if (VIRT_ADDR_SPACE_HIGH_END > KERNEL_VIRTUAL_OFFSET) {
        PrintKernelWarning("Virtual address space intersects kernel mapping window\n");
    }

    if (g_multiboot_info_addr >= kernel_start && g_multiboot_info_addr < kernel_end) {
        PrintKernelWarning("Multiboot info overlaps with kernel\n");
    }

    PrintKernelSuccess("System: Memory layout validated\n");
}

static void PrintBootstrapSummary(void) {
    PrintKernel("\n[BOOTSTRAP] Summary:\n");

    // Count page table pages used
    uint64_t pt_pages = 0;
    for (uint64_t i = 0x100000 / PAGE_SIZE; i < total_pages; i++) {
        if (!IsPageFree(i)) {
            uint64_t addr = i * PAGE_SIZE;
            // Heuristic: if it's not kernel or stack, likely a page table
            if (addr < (uint64_t)_kernel_phys_start ||
                addr >= (uint64_t)_kernel_phys_end) {
                pt_pages++;
            }
        }
    }

    PrintKernel("  Identity mapping: 4GB (");
    PrintKernelInt(IDENTITY_MAP_SIZE / (1024 * 1024 * 1024));
    PrintKernel("GB)\n");

    PrintKernel("  Page tables: ~");
    PrintKernelInt(pt_pages);
    PrintKernel(" pages (");
    PrintKernelInt((pt_pages * PAGE_SIZE) / 1024);
    PrintKernel("KB)\n");

    PrintKernel("  Bootstrap complete\n");
}


// Pre-eXecutionSystem 1
void PXS1(const uint32_t info) {
    PICMaskAll();
    int sret = SerialInit();
    if (sret != 0) {
        PrintKernelWarning("[WARN] COM1 failed, probing other COM ports...\n");
        if (SerialInitPort(COM2) != 0 && SerialInitPort(COM3) != 0 &&SerialInitPort(COM4) != 0) {
            PrintKernelWarning("[WARN] No serial ports initialized. Continuing without serial.\n");
        } else {
            PrintKernelSuccess("System: Serial driver initialized on fallback port\n");
        }
    } else {
        PrintKernelSuccess("System: Serial driver initialized on COM1\n");
    }

    if (VBEInit(info) != 0) {
        PrintKernelError("System: Failed to initialize VBE and graphical environment\n");
    } else {
        PrintKernelSuccess("System: VBE driver initialized\n");
    }

    PrintKernel("System: Starting Console...\n");
    ConsoleInit();
    PrintKernelSuccess("System: Console initialized\n");

#ifndef VF_CONFIG_EXCLUDE_EXTRA_OBJECTS
    VBEShowSplash();
#endif

#ifdef VF_CONFIG_SNOOZE_ON_BOOT
    Snooze();
#endif

    PrintKernel("System: Parsing MULTIBOOT2 info...\n");
    ParseMultibootInfo(info);
    PrintKernelSuccess("System: MULTIBOOT2 info parsed\n");

    PrintKernel("System: Initializing memory...\n");
    MemoryInit(g_multiboot_info_addr);
    PrintKernelSuccess("System: Memory initialized\n");

    // Create new PML4 with memory validation (ensure identity-mapped physical page)
    void* pml4_phys = NULL;
    for (int attempt = 0; attempt < 64; attempt++) {
        void* candidate = AllocPage();
        if (!candidate) break;
        if ((uint64_t)candidate < IDENTITY_MAP_SIZE) { pml4_phys = candidate; break; }
        FreePage(candidate);
    }
    if (!pml4_phys) PANIC("Failed to allocate PML4 in identity-mapped memory");

    FastZeroPage(pml4_phys);
    uint64_t pml4_addr = (uint64_t)pml4_phys;

    PrintKernelSuccess("System: Bootstrap: Identity mapping...\n");

    for (uint64_t paddr = 0; paddr < IDENTITY_MAP_SIZE; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr, paddr, PAGE_WRITABLE);

        if (paddr / PAGE_SIZE % 32768 == 0) {
            PrintKernel(".");
        }
    }
    PrintKernel("\n");

    PrintKernelSuccess("System: Bootstrap: Mapping kernel...\n");
    uint64_t kernel_start = (uint64_t)_kernel_phys_start & ~0xFFF;
    uint64_t kernel_end = ((uint64_t)_kernel_phys_end + 0xFFF) & ~0xFFF;
    for (uint64_t paddr = kernel_start; paddr < kernel_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }

    PrintKernelSuccess("System: Bootstrap: Mapping kernel stack...\n");
    uint64_t stack_phys_start = (uint64_t)kernel_stack & ~0xFFF;
    uint64_t stack_phys_end = ((uint64_t)kernel_stack + KERNEL_STACK_SIZE + 0xFFF) & ~0xFFF;

    for (uint64_t paddr = stack_phys_start; paddr < stack_phys_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }

    PrintKernelSuccess("System: Page tables prepared. Switching to virtual addressing...\n");
    const uint64_t new_stack_top = ((uint64_t)kernel_stack + KERNEL_VIRTUAL_OFFSET) + KERNEL_STACK_SIZE;
    const uint64_t higher_half_entry = (uint64_t)&KernelMainHigherHalf + KERNEL_VIRTUAL_OFFSET;

    PrintKernel("KernelMainHigherHalf addr: ");
    PrintKernelHex((uint64_t)&KernelMainHigherHalf);
    PrintKernel(", calculated entry: ");
    PrintKernelHex(higher_half_entry);
    PrintKernel("\n");

    SwitchToHigherHalf(pml4_addr, higher_half_entry, new_stack_top);
}

void MakeRoot() {
    PrintKernel("INITRD: Creating rootfs on /...\n");
    //======================================================================
    // 1. Core Operating System - (Largely Read-Only at Runtime)
    //======================================================================
    FsMkdir(SystemDir);
    FsMkdir(SystemKernel);      // Kernel executable, modules, and symbols
    FsCreateFile(SystemKernelLog);
    FsMkdir(SystemBoot);        // Bootloader and initial ramdisk images
    FsMkdir(SystemDrivers);     // Core hardware drivers bundled with the OS
    FsMkdir(SystemLibraries);   // Essential shared libraries (libc, etc.)
    FsMkdir(SystemServices);    // Executables for core system daemons
    FsMkdir(SystemResources);   // System-wide resources like fonts, icons, etc.

    //======================================================================
    // 2. Variable Data and User Installations - (Read-Write)
    //======================================================================
    FsMkdir(DataDir);
    FsMkdir(DataApps);          // User-installed applications reside here
    FsMkdir(DataConfig);        // System-wide configuration files
    FsMkdir(DataCache);         // System-wide caches
    FsMkdir(DataLogs);          // System and application logs
    FsMkdir(DataSpool);         // Spool directory for printing, mail, etc.
    FsMkdir(DataTemp);          // Temporary files that should persist across reboots

    //======================================================================
    // 3. Hardware and Device Tree - (Virtual, managed by kernel)
    //======================================================================
    FsMkdir(DevicesDir);
    FsMkdir(DevicesCpu);        // Info for each CPU core (cpuid, status, etc.)
    FsMkdir(DevicesPci);        // Hierarchy of PCI/PCIe devices
    FsMkdir(DevicesUsb);        // Hierarchy of USB devices
    FsMkdir(DevicesStorage);    // Block devices like disks and partitions (hda, sda)
    FsMkdir(DevicesInput);      // Keyboards, mice, tablets
    FsMkdir(DevicesGpu);        // Graphics processors
    FsMkdir(DevicesNet);        // Network interfaces (eth0, wlan0)
    FsMkdir(DevicesAcpi);       // ACPI tables and power information


    //======================================================================
    // 4. User Homes
    //======================================================================
    FsMkdir(UserDir);
    FsMkdir("/Users/Admin");        // Example administrator home
    FsMkdir("/Users/Admin/Desktop");
    FsMkdir("/Users/Admin/Documents");
    FsMkdir("/Users/Admin/Downloads");


    //======================================================================
    // 5. Live System State - (In-memory tmpfs, managed by kernel)
    //======================================================================
    FsMkdir(RuntimeDir);
    FsMkdir(RuntimeProcesses);  // A directory for each running sched by PID
    FsMkdir(RuntimeServices);   // Status and control files for running services
    FsMkdir(RuntimeIPC);        // For sockets and other inter-sched communication
    FsMkdir(RuntimeMounts);     // Information on currently mounted filesystems
}

// Pre-eXecutionSystem 2
static InitResultT PXS2(void) {
#ifndef VF_CONFIG_VM_HOST
    // CPU feature validation
    CPUFeatureValidation();
#endif

    // Print bootstrap summary
    PrintBootstrapSummary();

    // Initialize virtual memory manager with validation
    PrintKernel("Info: Initializing virtual memory manager...\n");
    VMemInit();
    PrintKernelSuccess("System: Virtual memory manager initialized\n");

    // Initialize kernel heap with memory statistics
    PrintKernel("Info: Initializing kernel heap...\n");
    KernelHeapInit();
    PrintKernelSuccess("System: Kernel heap initialized\n");

    // NEW: Initialize memory pools early for efficient small allocations
    PrintKernel("Info: Initializing memory pools...\n");
    InitDefaultPools();
    PrintKernelSuccess("System: Memory pools initialized\n");

    // NEW: Display detailed memory statistics
    PrintKernel("Info: Initial memory statistics:\n");
    MemoryStats stats;
    GetDetailedMemoryStats(&stats);
    PrintKernel("  Physical: ");
    PrintKernelInt(stats.free_physical_bytes / (1024*1024));
    PrintKernel("MB free, ");
    PrintKernelInt(stats.fragmentation_score);
    PrintKernel("% fragmented\n");
    PrintVMemStats();

    PrintKernel("Info: Initializing GDT...\n");
    GdtInit();
    PrintKernelSuccess("System: GDT initialized\n");

    // Initialize CPU features
    PrintKernel("Info: Initializing CPU features...\n");
    CpuInit();
    PrintKernelSuccess("System: CPU features initialized\n");

    // Initialize IDT
    PrintKernel("Info: Initializing IDT...\n");
    IdtInstall();
    PrintKernelSuccess("System: IDT initialized\n");

    // Initialize APIC
    PrintKernel("Info: Installing APIC...\n");
    if (!ApicInstall()) PANIC("Failed to initialize APIC");
    ApicTimerInstall(250);
    PrintKernelSuccess("System: APIC Installed\n");

#ifdef VF_CONFIG_ENFORCE_MEMORY_PROTECTION
    PrintKernel("Info: Final memory health check...\n");
    GetDetailedMemoryStats(&stats);
    if (stats.fragmentation_score > 50) {
        PrintKernelWarning("[WARN] High memory fragmentation detected\n");
    }
    // Memory protection
    StackGuardInit();
    SetupMemoryProtection();
#endif

#ifdef VF_CONFIG_ENABLE_PS2
    // Initialize keyboard
    PrintKernel("Info: Initializing PS/2 driver...\n");
    PS2Init();
    PrintKernelSuccess("System: PS/2 driver initialized\n");
#endif

#ifdef VF_CONFIG_USE_VFSHELL
    // Initialize shell
    PrintKernel("Info: Initializing shell...\n");
    ShellInit();
    PrintKernelSuccess("System: Shell initialized\n");
#endif

#ifdef VF_CONFIG_ENABLE_IDE
    // Initialize IDE driver
    PrintKernel("Info: Initializing IDE driver...\n");
    const int ide_result = IdeInit();
    if (ide_result == IDE_OK) {
        PrintKernelSuccess("System: IDE driver initialized\n");

        // Explicitly initialize FAT12 before VFS
        PrintKernel("Info: Initializing FAT12...\n");
        if (Fat1xInit(0) == 0) {
            PrintKernelSuccess("System: FAT1x Driver initialized\n");
        } else {
            PrintKernelWarning("FAT1x initialization failed\n");
        }

        if (Ext2Init(0) == 0) {
            PrintKernelSuccess("System: Ext2 Driver initialized\n");
        } else {
            PrintKernelWarning("Ext2 initialization failed\n");
        }
    } else {
        PrintKernelWarning(" IDE initialization failed - no drives detected\n");
        PrintKernelWarning(" Skipping FAT1x & EXT2 initialization\n");
    }
#endif

    // Initialize ram filesystem
    PrintKernel("Info: Initializing VFRFS...\n");
    FsInit();
    PrintKernelSuccess("System: VFRFS (VoidFrame RamFS) initialized\n");

    // Initrd
    MakeRoot();
    PrintKernelSuccess("System: INITRD (Stage 1) initialized\n");

    // Initialize VFS
    PrintKernel("Info: Initializing VFS...\n");
    VfsInit();
    PrintKernelSuccess("System: VFS initialized\n");

#ifdef VF_CONFIG_LOAD_MB_MODULES
    // Load multiboot modules
    PrintKernel("Info: Loading multiboot modules...\n");
    InitRDLoad();
    PrintKernelSuccess("System: Multiboot modules loaded\n");
#endif

#ifdef VF_CONFIG_ENFORCE_MEMORY_PROTECTION
    ValidateMemoryLayout();
    PrintKernel("Info: Checking huge page support...\n");
    if (!CheckHugePageSupport()) PrintKernel("System: Huge pages not available\n");
    else PrintKernelSuccess("System: Huge pages available\n");
#endif

#ifdef VF_CONFIG_ENABLE_ISA
    PrintKernel("Info: Initializing ISA bus...\n");
    IsaInitBus();
    PrintKernelSuccess("System: ISA bus initialized\n");

    PrintKernel("Info: Scanning ISA devices...\n");
    IsaAutoDetect();
    IsaPrintDevices();
#endif

#ifdef VF_CONFIG_ENABLE_PCI
    PrintKernel("Info: Scanning PCI devices...\n");
    PciInit();
    PrintKernelSuccess("System: PCI devices scanned\n");

    PrintKernel("Info: Initializing Network Stack...\n");
    Net_Initialize();
    PrintKernelSuccess("System: Network Stack initialized\n");
#endif

#ifdef VF_CONFIG_ENABLE_GENERIC_SOUND
    PrintKernel("Info: Initializing PC Speaker...\n");
    PCSpkr_Init();
    PrintKernelSuccess("System: PC Speaker initialized\n");

    PrintKernel("Info: Initializing AHCI Driver...\n");
#endif

#ifdef VF_CONFIG_ENABLE_AHCI
    if (AHCI_Init() == 0) {
        PrintKernelSuccess("System: AHCI Driver initialized\n");
    } else {
        PrintKernelWarning("AHCI initialization failed\n");
    }
#endif

#ifdef VF_CONFIG_ENABLE_VMWARE_SVGA_II
    if (SVGAII_DetectAndInitialize()) {
        g_svgaII_active = true;
        PrintKernelSuccess("System: VMware SVGA II driver initialized\n");
    } else {
        PrintKernelWarning("VMware SVGA II driver not detected\n");
    }
#endif

#ifdef VF_CONFIG_ENABLE_XHCI
    PrintKernel("Info: Initializing xHCI...\n");
    xHCIInit();
    PrintKernelSuccess("System: xHCI initialized\n");
#endif

#ifdef VF_CONFIG_ENABLE_LPT
    PrintKernel("Info: Initializing LPT Driver...\n");
    LPT_Init();
    PrintKernelSuccess("System: LPT Driver initialized\n");
#endif

#ifdef VF_CONFIG_SCHED_MLFQ // Make calls to MLFQGetCurrentProcess() returns NULL for as long as possible before interrupts are enabled
    // Initialize Process Management
    PrintKernel("Info: Initializing MLFQ scheduler...\n");
    MLFQSchedInit();
    PrintKernelSuccess("System: MLFQ scheduler initialized\n");
#endif

    return INIT_SUCCESS;
}

void A20Test(void) {
    volatile uint32_t *low = (uint32_t*)0x000000;
    volatile uint32_t *high = (uint32_t*)0x100000;

    *low = 0x12345678;
    *high = 0x87654321;

    if (*low == *high) PrintKernelWarning("A20 is disabled - memory is contiguous\n");
    else PrintKernelSuccess("A20 is enabled - memory is not contiguous\n");
}

asmlinkage void KernelMain(const uint32_t magic, const uint32_t info) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        ClearScreen();
        PrintKernelError("Magic: ");
        PrintKernelHex(magic);
        PANIC("Unrecognized Multiboot2 magic.");
    }

    A20Test();

    console.buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;

    PrintKernelSuccess("System: VoidFrame Kernel - Version 0.0.2-development3 loaded\n");
    PrintKernel("Magic: ");
    PrintKernelHex(magic);
    PrintKernel(", Info: ");
    PrintKernelHex(info);
    PrintKernel("\n");

    PXS1(info);
}

void KernelMainHigherHalf(void) {
    PrintKernelSuccess("System: Successfully jumped to higher half.\n");

    // Initialize core systems
    PXS2();

#ifdef VF_CONFIG_SNOOZE_ON_BOOT
    ClearScreen();
    Unsnooze();
#endif

    g_HasKernelStarted = true;

#ifdef VF_CONFIG_AUTOMATIC_POST
    ExecuteCommand("post");
#endif

    PrintKernelSuccess("System: Kernel initialization complete\n");
    PrintKernelSuccess("System: Initializing interrupts...\n");

    sti();

    while (1) {
        if (g_svgaII_active || VBEIsInitialized()) {
            WindowManagerRun();
            MLFQYield();
        } else {
            MLFQYield();
        }
    }

    __builtin_unreachable();
}
