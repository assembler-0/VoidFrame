#include "Kernel.h"
#include "Console.h"
#include "FAT12.h"
#include "Fs.h"
#include "Gdt.h"
#include "Ide.h"
#include "Idt.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "MemPool.h"
#include "Memory.h"
#include "Multiboot2.h"
#include "PCI/PCI.h"
#include "PS2.h"
#include "Paging.h"
#include "Panic.h"
#include "Pic.h"
#include "Process.h"
#include "ethernet/RTL8139.h"
#include "Serial.h"
#include "Shell.h"
#include "StackGuard.h"
#include "Syscall.h"
#include "VFS.h"
#include "VMem.h"
#include "VesaBIOSExtension.h"
#include "stdbool.h"
#include "stdint.h"

void KernelMainHigherHalf(void);
#define KERNEL_STACK_SIZE (16 * 1024) // 16KB stack
static uint8_t kernel_stack[KERNEL_STACK_SIZE]; // Statically allocate for simplicity
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

// Global variable to store the Multiboot2 info address
static uint32_t g_multiboot_info_addr = 0;

void ParseMultibootInfo(uint32_t info) {
    g_multiboot_info_addr = info;
    PrintKernel("[INFO] Parsing Multiboot2 info...\n");
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
        }
        // Move to the next tag, ensuring 8-byte alignment
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
    PrintKernelSuccess("[SYSTEM] Multiboot2 info parsed.\n");
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


// Memory hardening functions
static void SetupMemoryProtection(void) {
    PrintKernel("[SYSTEM] Setting up memory protection...\n");

    // Check CPUID for SMEP/SMAP support
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    bool protection_enabled = false;

    // Enable SMEP if supported (bit 7 in EBX from CPUID leaf 7)
    if (ebx & (1 << 7)) {
        cr4 |= (1ULL << 20);  // CR4.SMEP
        PrintKernel("[SYSTEM] SMEP enabled\n");
        protection_enabled = true;
    }

    // Enable SMAP if supported (bit 20 in EBX from CPUID leaf 7)
    if (ebx & (1 << 20)) {
        cr4 |= (1ULL << 21);  // CR4.SMAP
        PrintKernel("[SYSTEM] SMAP enabled\n");
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
        PrintKernel("[SYSTEM] NX bit enabled\n");
        protection_enabled = true;
    }

    // Enable PCID for faster context switches
    __asm__ volatile("cpuid" : "=a"(eax), "=c"(ecx) : "a"(1) : "ebx", "edx");
    if (ecx & (1 << 17)) {
        cr4 |= (1ULL << 17);  // CR4.PCIDE
        PrintKernel("[SYSTEM] PCID enabled\n");
        protection_enabled = true;
    }

    // Write back the modified CR4
    if (protection_enabled) {
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
        PrintKernelSuccess("[SYSTEM] Memory protection configured\n");
    } else {
        PrintKernel("[SYSTEM] No memory protection features available\n");
    }
}

static bool CheckHugePageSupport(void) {
    uint32_t eax, ebx, ecx, edx;

    // Check for PSE (Page Size Extension) - required for 2MB pages
    __asm__ volatile("cpuid" : "=a"(eax), "=d"(edx) : "a"(1) : "ebx", "ecx");
    if (!(edx & (1 << 3))) {
        PrintKernel("[INFO] PSE not supported - no huge pages\n");
        return false;
    }

    // Check for PSE-36 for extended physical addressing
    if (edx & (1 << 17)) {
        PrintKernel("[INFO] PSE-36 supported\n");
    }

    return true;
}

void SystemInitS1(const uint32_t info) {
    int sret = SerialInit();

    if (sret != 0) {
        PrintKernelWarning("[WARN] COM1 failed, probing other COM ports...\n");
        if (SerialInitPort(COM2) != 0 && SerialInitPort(COM3) != 0 &&SerialInitPort(COM4) != 0) {
            PrintKernelWarning("[WARN] No serial ports initialized. Continuing without serial.\n");
        } else {
            PrintKernelSuccess("[SYSTEM] Serial driver initialized on fallback port\n");
        }
    } else {
        PrintKernelSuccess("[SYSTEM] Serial driver initialized on COM1\n");
    }

    if (VBEInit(info) != 0) {
        PrintKernelError("[SYSTEM] Failed to initialize VBE and graphical environment");
    }
    PrintKernel("[SYSTEM] Starting Console...\n");
    ConsoleInit();
    PrintKernelSuccess("[SYSTEM] Console initialized\n");

    VBEShowSplash();

    PrintKernel("[SYSTEM] Parsing MULTIBOOT2 info...\n");
    ParseMultibootInfo(info);
    PrintKernelSuccess("[SYSTEM] MULTIBOOT2 info parsed\n");

    PrintKernel("[SYSTEM] Initializing memory...\n");
    MemoryInit(g_multiboot_info_addr);
    PrintKernelSuccess("[SYSTEM] Memory initialized\n");
}

// Enhanced SystemInitS2 function with memory enhancements
static InitResultT SystemInitS2(void) {
    // Initialize virtual memory manager with validation
    PrintKernel("[INFO] Initializing virtual memory manager...\n");
    VMemInit();
    PrintKernelSuccess("[SYSTEM] Virtual memory manager initialized\n");

    // Initialize kernel heap with memory statistics
    PrintKernel("[INFO] Initializing kernel heap...\n");
    KernelHeapInit();
    PrintKernelSuccess("[SYSTEM] Kernel heap initialized\n");

    // NEW: Initialize memory pools early for efficient small allocations
    PrintKernel("[INFO] Initializing memory pools...\n");
    InitDefaultPools();
    PrintKernelSuccess("[SYSTEM] Memory pools initialized\n");

    // NEW: Display detailed memory statistics
    PrintKernel("[INFO] Initial memory statistics:\n");
    MemoryStats stats;
    GetDetailedMemoryStats(&stats);
    PrintKernel("  Physical: ");
    PrintKernelInt(stats.free_physical_bytes / (1024*1024));
    PrintKernel("MB free, ");
    PrintKernelInt(stats.fragmentation_score);
    PrintKernel("% fragmented\n");
    PrintVMemStats();

    PrintKernel("[INFO] Initializing GDT...\n");
    GdtInit();
    PrintKernelSuccess("[SYSTEM] GDT initialized\n");

    // Initialize CPU features
    PrintKernel("[INFO] Initializing CPU features...\n");
    CpuInit();
    PrintKernelSuccess("[SYSTEM] CPU features initialized\n");

    // Initialize IDT
    PrintKernel("[INFO] Initializing IDT...\n");
    IdtInstall();
    PrintKernelSuccess("[SYSTEM] IDT initialized\n");

    // Initialize System Calls
    PrintKernel("[INFO] Initializing system calls...\n");
    SyscallInit();
    PrintKernelSuccess("[SYSTEM] System calls initialized\n");

    // Initialize PIC
    PrintKernel("[INFO] Initializing PIC...\n");
    PicInstall();
    PrintKernelSuccess("[SYSTEM] PIC initialized\n");

    // Initialize keyboard
    PrintKernel("[INFO] Initializing keyboard...\n");
    PS2Init();
    PrintKernelSuccess("[SYSTEM] Keyboard initialized\n");

    // Initialize shell
    PrintKernel("[INFO] Initializing shell...\n");
    ShellInit();
    PrintKernelSuccess("[SYSTEM] Shell initialized\n");

    // Initialize Process Management
    PrintKernel("[INFO] Initializing process management...\n");
    ProcessInit();
    PrintKernelSuccess("[SYSTEM] Process management initialized\n");

    // Initialize IDE driver
    PrintKernel("[INFO] Initializing IDE driver...\n");
    int ide_result = IdeInit();
    if (ide_result == IDE_OK) {
        PrintKernelSuccess("[SYSTEM] IDE driver initialized\n");

        // Explicitly initialize FAT12 before VFS
        PrintKernel("[INFO] Initializing FAT12...\n");
        if (Fat12Init(0) == 0) {
            PrintKernelSuccess("[SYSTEM] FAT12 Driver initialized\n");
        } else {
            PrintKernelWarning("[WARN] FAT12 initialization failed\n");
        }
    } else {
        PrintKernelWarning("[WARN] IDE initialization failed - no drives detected\n");
        PrintKernelWarning("[WARN] Skipping FAT12 initialization\n");
    }

    // Initialize ram filesystem
    PrintKernel("[INFO] Initializing VFRFS...\n");
    FsInit();
    PrintKernelSuccess("[SYSTEM] VFRFS (VoidFrame RamFS) initialized\n");

    // Initialize VFS
    PrintKernel("[INFO] Initializing VFS...\n");
    VfsInit();
    PrintKernelSuccess("[SYSTEM] VFS initialized\n");

    // NEW: Check if huge pages should be enabled
    PrintKernel("[INFO] Checking huge page support...\n");
    if (CheckHugePageSupport()) {
        PrintKernelSuccess("[SYSTEM] Huge pages available\n");
    }

    // Setup memory protection LAST - after all systems are ready
    StackGuardInit();
    SetupMemoryProtection();

    PrintKernel("[INFO] Scanning PCI devices...\n");
    PciEnumerate();
    PrintKernelSuccess("[SYSTEM] PCI devices scanned\n");

    PrintKernel("[INFO] Initializing RTL8139 Driver...\n");
    Rtl8139_Init();
    PrintKernelSuccess("[SYSTEM] RTL8139 Driver initialized\n");

    // NEW: Final memory health check
    PrintKernel("[INFO] Final memory health check...\n");
    GetDetailedMemoryStats(&stats);
    if (stats.fragmentation_score > 50) {
        PrintKernelWarning("[WARN] High memory fragmentation detected\n");
    }

    return INIT_SUCCESS;
}

void KernelMain(const uint32_t magic, const uint32_t info) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        ClearScreen();
        PrintKernelError("Magic: ");
        PrintKernelHex(magic);
        PANIC("Unrecognized Multiboot2 magic.");
    }

    SystemInitS1(info);

    console.buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;
    
    ClearScreen();
    PrintKernelSuccess("[SYSTEM] VoidFrame Kernel - Version 0.0.1-beta loaded\n");
    PrintKernel("Magic: ");
    PrintKernelHex(magic);
    PrintKernel(", Info: ");
    PrintKernelHex(info);
    PrintKernel("\n");

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
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Identity mapping...\n");
    
    for (uint64_t paddr = 0; paddr < IDENTITY_MAP_SIZE; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr, paddr, PAGE_WRITABLE);
        
        if (paddr / PAGE_SIZE % 32768 == 0) {
            PrintKernel(".");
        }
    }
    PrintKernel("\n");
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Mapping kernel...\n");
    uint64_t kernel_start = (uint64_t)_kernel_phys_start & ~0xFFF;
    uint64_t kernel_end = ((uint64_t)_kernel_phys_end + 0xFFF) & ~0xFFF;
    for (uint64_t paddr = kernel_start; paddr < kernel_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Mapping kernel stack...\n");
    uint64_t stack_phys_start = (uint64_t)kernel_stack & ~0xFFF;
    uint64_t stack_phys_end = ((uint64_t)kernel_stack + KERNEL_STACK_SIZE + 0xFFF) & ~0xFFF;

    for (uint64_t paddr = stack_phys_start; paddr < stack_phys_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }

    PrintKernelSuccess("[SYSTEM] Page tables prepared. Switching to virtual addressing...\n");
    const uint64_t new_stack_top = ((uint64_t)kernel_stack + KERNEL_VIRTUAL_OFFSET) + KERNEL_STACK_SIZE;
    const uint64_t higher_half_entry = (uint64_t)&KernelMainHigherHalf + KERNEL_VIRTUAL_OFFSET;
    EnablePagingAndJump(pml4_addr, higher_half_entry, new_stack_top);
}

static void ValidateMemoryLayout(void) {
    PrintKernel("[SYSTEM] Validating memory layout...\n");

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

    // Check for dangerous overlaps
    uint64_t stack_start = (uint64_t)kernel_stack;
    uint64_t stack_end = stack_start + KERNEL_STACK_SIZE;

    if ((stack_start >= kernel_start && stack_start < kernel_end) ||
        (stack_end > kernel_start && stack_end <= kernel_end)) {
        PrintKernelWarning("[WARNING] Stack overlaps with kernel code\n");
        }

    // NEW: Check multiboot info location
    if (g_multiboot_info_addr >= kernel_start && g_multiboot_info_addr < kernel_end) {
        PrintKernelWarning("[WARNING] Multiboot info overlaps with kernel\n");
    }

    // NEW: Validate virtual address space boundaries
    if (VIRT_ADDR_SPACE_START >= KERNEL_SPACE_START) {
        PrintKernelError("[ERROR] Virtual address space overlaps with kernel space\n");
    }


    PrintKernelSuccess("[SYSTEM] Memory layout validated\n");
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

void KernelMainHigherHalf(void) {
    PrintKernelSuccess("[SYSTEM] Successfully jumped to higher half. Virtual memory is active.\n");
    
    // Memory safety validation
    ValidateMemoryLayout();

    // Print bootstrap summary
    PrintBootstrapSummary();

    // Initialize core systems
    SystemInitS2();

    PrintKernelSuccess("[SYSTEM] Kernel initialization complete\n");
    PrintKernelSuccess("[SYSTEM] Initializing interrupts...\n");

    asm volatile("sti");

    while (1) {
        asm volatile("hlt");
    }
}