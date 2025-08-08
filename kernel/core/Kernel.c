#include "Kernel.h"
#include "AsmHelpers.h"
#include "Console.h"
#include "Gdt.h"
#include "Idt.h"
#include "Keyboard.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Memory.h"
#include "Multiboot2.h"
#include "Panic.h"
#include "Pic.h"
#include "Process.h"
#include "Shell.h"
#include "Syscall.h"
#include "VMem.h"
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

void BootstrapMapPage(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // Input validation for safety
    if (!pml4_phys || (pml4_phys & 0xFFF)) PANIC("Invalid PML4 address");
    // align
    if (vaddr & 0xFFF || paddr & 0xFFF) {
        vaddr &= ~0xFFF;  // Page-align virtual address
        paddr &= ~0xFFF;  // Page-align physical address
    }
    
    uint64_t* pml4 = (uint64_t*)pml4_phys;

    // 1. Get/Create PDPT with validation
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_phys;
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        pdpt_phys = (uint64_t)AllocPage();
        if (!pdpt_phys) PANIC("BootstrapMapPage: Out of memory for PDPT");
        if (pdpt_phys & 0xFFF) PANIC("PDPT not aligned");
        FastZeroPage((void*)pdpt_phys);
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
        pd_phys = (uint64_t)AllocPage();
        if (!pd_phys) PANIC("BootstrapMapPage: Out of memory for PD");
        FastZeroPage((void*)pd_phys);
        pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pd_phys = pdpt[pdpt_idx] & PT_ADDR_MASK;
    }

    // 3. Get/Create PT
    uint64_t* pd = (uint64_t*)pd_phys;
    const int pd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pt_phys;
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        pt_phys = (uint64_t)AllocPage();
        if (!pt_phys) PANIC("BootstrapMapPage: Out of memory for PT");
        FastZeroPage((void*)pt_phys);
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pt_phys = pd[pd_idx] & PT_ADDR_MASK;
    }

    // 4. Set the final PTE
    uint64_t* pt = (uint64_t*)pt_phys;
    const int pt_idx = (vaddr >> 12) & 0x1FF;
    pt[pt_idx] = paddr | flags | PAGE_PRESENT;
}

// Memory hardening functions
static void SetupMemoryProtection(void) {
    PrintKernel("[SYSTEM] Setting up memory protection...\n");

    // Enable SMEP/SMAP if available
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    PrintKernel("[SYSTEM] CR4 features enabled\n");
    PrintKernelSuccess("[SYSTEM] Memory protection configured\n");
}


static InitResultT CoreInit(void) {
    // Initialize virtual memory manager with validation
    PrintKernel("[INFO] Initializing virtual memory manager...\n");
    VMemInit();
    PrintKernelSuccess("[SYSTEM] Virtual memory manager initialized\n");
    
    // Initialize kernel heap with memory statistics
    PrintKernel("[INFO] Initializing kernel heap...\n");
    KernelHeapInit();
    PrintKernelSuccess("[SYSTEM] Kernel heap initialized\n");
    
    // Display memory statistics for monitoring
    PrintKernel("[INFO] Memory statistics:\n");
    // Add memory usage display here if available

    PrintKernel("[INFO] Initializing GDT...\n");
    GdtInit();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] GDT initialized\n");

    // Initialize CPU features
    PrintKernel("[INFO] Initializing CPU features...\n");
    CpuInit();
    PrintKernelSuccess("[SYSTEM] CPU features initialized\n");

    // Initialize IDT
    PrintKernel("[INFO] Initializing IDT...\n");
    IdtInstall();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] IDT initialized\n");

    // Initialize System Calls
    PrintKernel("[INFO] Initializing system calls...\n");
    SyscallInit();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] System calls initialized\n");

    // Initialize PIC
    PrintKernel("[INFO] Initializing PIC...\n");
    PicInstall();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] PIC initialized\n");

    // Initialize keyboard
    PrintKernel("[INFO] Initializing keyboard...\n");
    KeyboardInit();
    PrintKernelSuccess("[SYSTEM] Keyboard initialized\n");
    
    // Initialize shell
    PrintKernel("[INFO] Initializing shell...\n");
    ShellInit();
    PrintKernelSuccess("[SYSTEM] Shell initialized\n");

    // Initialize Process Management
    PrintKernel("[INFO] Initializing process management...\n");
    ProcessInit();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] Process management initialized\n");

    // Setup memory protection LAST - after all systems are ready
    SetupMemoryProtection();
    return INIT_SUCCESS;
}

void KernelMain(const uint32_t magic, const uint32_t info) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        ClearScreen();
        PrintKernelError("Magic: ");
        PrintKernelHex(magic);
        PANIC("Unrecognized Multiboot2 magic.");
    }

    console.buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;
    ClearScreen();
    PrintKernelSuccess("[SYSTEM] VoidFrame Kernel - Version 0.0.1-beta loaded\n");
    PrintKernel("Magic: ");
    PrintKernelHex(magic);
    PrintKernel(", Info: ");
    PrintKernelHex(info);
    PrintKernel("\n\n");
    ParseMultibootInfo(info);
    
    // Initialize physical memory manager first
    MemoryInit(g_multiboot_info_addr);
    
    // Create new PML4 with memory validation
    void* pml4_phys = AllocPage();
    if (!pml4_phys) PANIC("Failed to allocate PML4");
    
    // Validate allocated page before use
    if ((uint64_t)pml4_phys & 0xFFF) PANIC("PML4 not page-aligned");
    if ((uint64_t)pml4_phys < 0x100000) PANIC("PML4 in low memory");
    
    FastZeroPage(pml4_phys);
    uint64_t pml4_addr = (uint64_t)pml4_phys;
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Identity mapping low memory...\n");
    // Batch allocate pages for better performance
    uint32_t pages_needed = IDENTITY_MAP_SIZE / PAGE_SIZE;
    PrintKernel("[INFO] Mapping ");
    PrintKernelInt(pages_needed);
    PrintKernel(" pages for identity mapping\n");
    
    for (uint64_t paddr = 0; paddr < IDENTITY_MAP_SIZE; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr, paddr, PAGE_WRITABLE);
        
        // Progress indicator for large mappings
        if ((paddr / PAGE_SIZE) % 1024 == 0) {
            PrintKernel(".");
        }
    }
    PrintKernel("\n");
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Mapping kernel...\n");
    uint64_t kernel_start = (uint64_t)_kernel_phys_start & ~0xFFF;  // Page-align
    uint64_t kernel_end = ((uint64_t)_kernel_phys_end + 0xFFF) & ~0xFFF;  // Round up
    for (uint64_t paddr = kernel_start; paddr < kernel_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }
    
    PrintKernelSuccess("[SYSTEM] Bootstrap: Mapping kernel stack with guard pages...\n");
    uint64_t stack_phys_start = (uint64_t)kernel_stack & ~0xFFF;
    uint64_t stack_phys_end = ((uint64_t)kernel_stack + KERNEL_STACK_SIZE + 0xFFF) & ~0xFFF;
    
    // Create guard page BEFORE stack (unmapped)
    PrintKernel("[GUARD] Stack guard page before stack (unmapped)\n");
    
    // Map actual stack
    for (uint64_t paddr = stack_phys_start; paddr < stack_phys_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_addr, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }
    
    // Create guard page AFTER stack (unmapped)
    PrintKernel("[GUARD] Stack guard page after stack (unmapped)\n");

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
    
    PrintKernelSuccess("[SYSTEM] Memory layout validated\n");
}

void KernelMainHigherHalf(void) {
    PrintKernelSuccess("[SYSTEM] Successfully jumped to higher half. Virtual memory is active.\n");
    
    // Memory safety validation
    ValidateMemoryLayout();
    
    // Initialize core systems
    CoreInit();

    PrintKernelSuccess("[SYSTEM] Kernel initialization complete\n");
    PrintKernelSuccess("[SYSTEM] Initializing interrupts...\n\n");
    
    asm volatile("sti");
    while (1) {
        if (ShouldSchedule()) {
            RequestSchedule();
        }
        asm volatile("hlt");
    }
}