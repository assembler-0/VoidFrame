/*
 * Kernel.c - VoidFrame Kernel Main Module
 * Modern C implementation with optimizations
 */
#include "stdint.h"
#include "Idt.h"
#include "Pic.h"
#include "Kernel.h"
#include "Memory.h"
#include "Process.h"
#include "Syscall.h"
#include "Gdt.h"
#include "Panic.h"
#include "stdbool.h"
#include "Multiboot2.h"
#include "AsmHelpers.h"
#include "MemOps.h"
#include "VMem.h"
#include "Splash.h"

void KernelMainHigherHalf(void);
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];
// VGA Constants
#define VGA_BUFFER_ADDR     0xB8000
#define VGA_WIDTH           80
#define VGA_HEIGHT          25
#define VGA_BUFFER_SIZE     (VGA_WIDTH * VGA_HEIGHT)
#define VGA_COLOR_DEFAULT   0x08
#define VGA_COLOR_SUCCESS   0x0B
#define VGA_COLOR_ERROR     0x0C
#define VGA_COLOR_WARNING   0x0E
// Console state
typedef struct {
    uint32_t line;
    uint32_t column;
    volatile uint16_t* buffer;
    uint8_t color;
} ConsoleT;

typedef enum {
    INIT_SUCCESS = 0,
    INIT_ERROR_GDT,
    INIT_ERROR_IDT,
    INIT_ERROR_SYSCALL,
    INIT_ERROR_PIC,
    INIT_ERROR_MEMORY,
    INIT_ERROR_PROCESS,
    INIT_ERROR_SECURITY
} InitResultT;

static ConsoleT console = {
    .line = 0,
    .column = 0,
    .buffer = (volatile uint16_t*)VGA_BUFFER_ADDR,
    .color = VGA_COLOR_DEFAULT
};
static volatile int lock = 0;
// Inline functions for better performance
static inline void ConsoleSetColor(uint8_t color) {
    console.color = color;
}

static inline uint16_t MakeVGAEntry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline void ConsolePutcharAt(char c, uint32_t x, uint32_t y, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    const uint32_t index = y * VGA_WIDTH + x;
    console.buffer[index] = MakeVGAEntry(c, color);
}

// Optimized screen clear using memset-like approach
void ClearScreen(void) {
    SpinLock(&lock);
    const uint16_t blank = MakeVGAEntry(' ', VGA_COLOR_DEFAULT);
    
    // Use 32-bit writes for better performance
    volatile uint32_t* buffer32 = (volatile uint32_t*)console.buffer;
    const uint32_t blank32 = ((uint32_t)blank << 16) | blank;
    const uint32_t size32 = (VGA_WIDTH * VGA_HEIGHT) / 2;
    
    for (uint32_t i = 0; i < size32; i++) {
        buffer32[i] = blank32;
    }
    
    console.line = 0;
    console.column = 0;
    SpinUnlock(&lock);;
}

// Optimized scrolling
static void ConsoleScroll(void) {
    // Move all lines up by one using memmove-like operation
    for (uint32_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        console.buffer[i] = console.buffer[i + VGA_WIDTH];
    }
    
    // Clear the last line
    const uint16_t blank = MakeVGAEntry(' ', console.color);
    const uint32_t last_line_start = (VGA_HEIGHT - 1) * VGA_WIDTH;
    
    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        console.buffer[last_line_start + i] = blank;
    }
}

// Optimized character output with bounds checking
static void ConsolePutchar(char c) {
    if (c == '\n') {
        console.line++;
        console.column = 0;
    } else if (c == '\r') {
        console.column = 0;
    } else if (c == '\t') {
        console.column = (console.column + 8) & ~7; // Align to 8
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    } else if (c >= 32) { // Printable characters only
        ConsolePutcharAt(c, console.column, console.line, console.color);
        console.column++;
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    }
    
    // Handle scrolling
    if (console.line >= VGA_HEIGHT) {
        ConsoleScroll();
        console.line = VGA_HEIGHT - 1;
    }
}

// Modern string output with length checking
void PrintKernel(const char* str) {
    if (!str) return;
    SpinLock(&lock);
    // Cache the original color
    const uint8_t original_color = console.color;

    for (const char* p = str; *p; p++) {
        ConsolePutchar(*p);
    }

    console.color = original_color;
    SpinUnlock(&lock);
}

// Colored output variants
void PrintKernelSuccess(const char* str) {
    ConsoleSetColor(VGA_COLOR_SUCCESS);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelError(const char* str) {
    ConsoleSetColor(VGA_COLOR_ERROR);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelWarning(const char* str) {
    ConsoleSetColor(VGA_COLOR_WARNING);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

// Optimized hex printing with proper formatting
void PrintKernelHex(uint64_t num) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19]; // "0x" + 16 hex digits + null terminator

    buffer[0] = '0';
    buffer[1] = 'x';

    if (num == 0) {
        buffer[2] = '0';
        buffer[3] = '\0';
        PrintKernel(buffer);
        return;
    }

    int pos = 18;
    buffer[pos--] = '\0';

    while (num > 0 && pos >= 2) {
        buffer[pos--] = hex_chars[num & 0xF];
        num >>= 4;
    }

    PrintKernel(&buffer[pos + 1]);
}

// Optimized integer printing with proper sign handling
void PrintKernelInt(int64_t num) {
    char buffer[21]; // Max digits for 64-bit signed integer + sign + null

    if (num == 0) {
        PrintKernel("0");
        return;
    }

    bool negative = num < 0;
    if (negative) num = -num;

    int pos = 20;
    buffer[pos--] = '\0';

    while (num > 0 && pos >= 0) {
        buffer[pos--] = '0' + (num % 10);
        num /= 10;
    }

    if (negative && pos >= 0) {
        buffer[pos--] = '-';
    }

    PrintKernel(&buffer[pos + 1]);
}

// Safe positioned printing
void PrintKernelAt(const char* str, uint32_t line, uint32_t col) {
    if (!str || line >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    const uint32_t saved_line = console.line;
    const uint32_t saved_col = console.column;
    
    console.line = line;
    console.column = col;
    
    // Print until end of line or string
    for (const char* p = str; *p && console.column < VGA_WIDTH; p++) {
        if (*p == '\n') break;
        ConsolePutchar(*p);
    }
    
    // Restore cursor position
    console.line = saved_line;
    console.column = saved_col;
}



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

        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            struct MultibootTagMmap* mmap_tag = (struct MultibootTagMmap*)tag;
            PrintKernel("    Memory Map Tag found! Entry size: ");
            PrintKernelInt(mmap_tag->entry_size);
            PrintKernel("\n");

            for (uint32_t i = 0; i < (mmap_tag->size - sizeof(struct MultibootTagMmap)) / mmap_tag->entry_size; i++) {
                struct MultibootMmapEntry* entry = (struct MultibootMmapEntry*)((uint8_t*)mmap_tag + sizeof(struct MultibootTagMmap) + (i * mmap_tag->entry_size));
                PrintKernel("      Addr: ");
                PrintKernelHex(entry->addr);
                PrintKernel(", Len: ");
                PrintKernelHex(entry->len);
                PrintKernel(", Type: ");
                PrintKernelInt(entry->type);
                PrintKernel("\n");
            }
        }
        // Move to the next tag, ensuring 8-byte alignment
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
    PrintKernelSuccess("[SYSTEM] Multiboot2 info parsed.\n");
}

void BootstrapMapPage(uint64_t pml4_phys, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t* pml4 = (uint64_t*)pml4_phys;

    // 1. Get/Create PDPT
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_phys;
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        pdpt_phys = (uint64_t)AllocPage();
        if (!pdpt_phys) PANIC("BootstrapMapPage: Out of memory for PDPT");
        FastZeroPage((void*)pdpt_phys);
        pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pdpt_phys = pml4[pml4_idx] & PT_ADDR_MASK;
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
    int pd_idx = (vaddr >> 21) & 0x1FF;
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
    int pt_idx = (vaddr >> 12) & 0x1FF;
    pt[pt_idx] = paddr | flags | PAGE_PRESENT;
}

static InitResultT CoreInit(void) {
    // Initialize GDT
    PrintKernel("[INFO] Initializing GDT...\n");
    GdtInit();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] GDT initialized\n");

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

    // Initialize Process Management
    PrintKernel("[INFO] Initializing process management...\n");
    ProcessInit();  // void function - assume success
    PrintKernelSuccess("[SYSTEM] Process management initialized\n");
    return INIT_SUCCESS;
}

void KernelMain(uint32_t magic, uint32_t info) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        ClearScreen();
        PrintKernelError("Magic: ");
        PrintKernelHex(magic);
        PANIC("Unrecognized Multiboot2 magic.");
    }
    ShowSplashScreen();
    PrintKernelSuccess("[SYSTEM] VoidFrame Kernel - Version 0.0.1-alpha loaded\n");
    PrintKernel("Magic: ");
    PrintKernelHex(magic);
    PrintKernel(", Info: ");
    PrintKernelHex(info);
    PrintKernel("\n\n");
    ParseMultibootInfo(info);
    MemoryInit(g_multiboot_info_addr);
    VMemInit();
    uint64_t pml4_phys = VMemGetPML4PhysAddr();

    uint64_t kernel_start = (uint64_t)_kernel_phys_start;
    uint64_t kernel_end = (uint64_t)_kernel_phys_end;

    PrintKernelSuccess("[SYSTEM] Bootstrap: Mapping kernel...\n");
    // Map the kernel itself using the bootstrap function
    for (uint64_t paddr = kernel_start; paddr < kernel_end; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_phys, paddr + KERNEL_VIRTUAL_OFFSET, paddr, PAGE_WRITABLE);
    }

    PrintKernelSuccess("[SYSTEM] Bootstrap: Identity mapping low memory...\n");
    // Map the first 4MB identity-mapped for safety (VGA, etc.)
    for (uint64_t paddr = 0; paddr < 4 * 1024 * 1024; paddr += PAGE_SIZE) {
        BootstrapMapPage(pml4_phys, paddr, paddr, PAGE_WRITABLE);
    }

    PrintKernelSuccess("[SYSTEM] Page tables prepared. Switching to virtual addressing...\n");

    uint64_t higher_half_entry = (uint64_t)&KernelMainHigherHalf;
    EnablePagingAndJump(pml4_phys, higher_half_entry);
}

void KernelMainHigherHalf(void) {
    CoreInit();
    PrintKernelSuccess("[SYSTEM] Successfully jumped to higher half. Virtual memory is active.\n");
    PrintKernel("[INFO] Creating security manager process...\n");
    uint64_t security_pid = CreateSecureProcess(SecureKernelIntegritySubsystem, PROC_PRIV_SYSTEM);
    if (!security_pid) {
        PrintKernelError("[FATAL] Cannot create SecureKernelIntegritySubsystem\n");
        PANIC("Critical security 1failure - cannot create security manager");
    }
    PrintKernelSuccess("[SYSTEM] Security manager created with PID: ");
    PrintKernelInt(security_pid);
    PrintKernel("\n");
    PrintKernelSuccess("[SYSTEM] Core system modules loaded\n");
    PrintKernelSuccess("[SYSTEM] Kernel initialization complete\n");
    PrintKernelSuccess("[SYSTEM] Transferring control to SecureKernelIntegritySubsystem...\n");
    PrintKernelSuccess("[SYSTEM] Initializing interrupts...\n\n");
    asm volatile("sti");
    while (1) {
        if (ShouldSchedule()) {
            RequestSchedule();
        }
        asm volatile("hlt");
    }
}