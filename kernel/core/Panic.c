#include "Panic.h"

// The ONLY necessary includes for display are now Console and Serial
#include "Console.h"
#include "KernelHeap.h"
#include "Memory.h"
#include "Serial.h"

#include "Process.h" // For Registers struct in PanicFromInterrupt
#include "VMem.h"
#include "VesaBIOSExtension.h"
#include "stdint.h"
#include "MemOps.h"
// --- Panic Context Structure (Unchanged) ---
typedef struct {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t error_code;
    const char* function;
    const char* file;
    int line;
} PanicContext;


// --- Self-Contained String Formatting Helpers ---
// These are kept here to format strings before printing, as the Console API prints directly.

static void U64ToHexStr(uint64_t value, char* buffer) {
    const char* hex_chars = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buffer[2 + i] = hex_chars[(value >> (60 - i * 4)) & 0xF];
    }
    buffer[18] = '\0';
}

static void U32ToDecStr(uint32_t value, char* buffer) {
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    char int_buffer[11];
    char* p = &int_buffer[10];
    *p = '\0';

    uint32_t temp = value;
    do {
        *--p = '0' + (temp % 10);
        temp /= 10;
    } while (temp > 0);

    // Basic strcpy
    char* d = buffer;
    while ((*d++ = *p++));
}

// --- High-Level Panic Screen Composition ---

void __attribute__((noreturn)) KernelPanicHandler(const char* message, uint64_t error_code, PanicContext* ctx) {
    asm volatile("cli");
    PrintKernel("[FATAL] - [KERNEL PANIC] - COLLECTING RESOURCES....\n");
    delay(2000000000);

    // Reentrancy guard to prevent recursive panics
    static volatile int in_panic = 0;
    if (__atomic_exchange_n(&in_panic, 1, __ATOMIC_ACQ_REL)) {
        goto hang;
    }

    // Determine display mode ONCE at the beginning
    int use_vbe_graphics = VBEIsInitialized();

    if (use_vbe_graphics) {
        // Pure VBE graphics path - show panic image ONLY
        VBEShowPanic();

        // Output to serial for debugging (no screen output)
        SerialWrite("[FATAL] - [KERNEL PANIC] -- ");
        if (message) SerialWrite(message);
        SerialWrite("\n");
        if (ctx) {
            SerialWrite("RIP: ");
            char hex[20];
            char dec_buffer[12];
            char temp_buffer[128];
            temp_buffer[0] = '\0';
            U64ToHexStr(ctx->rip, hex);
            SerialWrite(hex);
            SerialWrite("\n");
            SerialWrite("- CPU CONTEXT -\n");
            SerialWrite("----------------------\n");

            U64ToHexStr(ctx->rip, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RIP:  "); strcat(temp_buffer, hex);
            SerialWrite(temp_buffer);
            SerialWrite("\n");

            U64ToHexStr(ctx->rsp, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RSP:  "); strcat(temp_buffer, hex);
            SerialWrite(temp_buffer);
            SerialWrite("\n");

            U64ToHexStr(ctx->rbp, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RBP:  "); strcat(temp_buffer, hex);
            SerialWrite(temp_buffer);
            SerialWrite("\n");

            U64ToHexStr(error_code, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "CODE: "); strcat(temp_buffer, hex);
            SerialWrite(temp_buffer);
            SerialWrite("\n");

            // --- Source Location ---
            SerialWrite("- SOURCE LOCATION -\n");
            SerialWrite("----------------------\n");

            if (ctx->file && ctx->file[0] != '\0') {
                temp_buffer[0] = '\0'; strcat(temp_buffer, "File: "); strcat(temp_buffer, ctx->file);
                SerialWrite(temp_buffer);
                SerialWrite("\n");

                temp_buffer[0] = '\0'; strcat(temp_buffer, "Func: "); strcat(temp_buffer, ctx->function);
                SerialWrite(temp_buffer);
                SerialWrite("\n");

                U32ToDecStr(ctx->line, dec_buffer);
                temp_buffer[0] = '\0'; strcat(temp_buffer, "Line: "); strcat(temp_buffer, dec_buffer);
                SerialWrite(temp_buffer);
                SerialWrite("\n");
            } else {
                SerialWrite("Unavailable\n");
            }
        }
    } else {
        // Pure text mode path - no graphics
        ClearScreen();  // Clear ONCE
        ConsoleSetColor(VGA_COLOR_WHITE);

        const int margin = 2;
        int line = 2;

        // Your existing text output code
        char temp_buffer[128];
        temp_buffer[0] = '\0';
        strcat(temp_buffer, "Your system has been halted due to an unrecoverable error.");
        PrintKernelAt(temp_buffer, line++, margin);

        temp_buffer[0] = '\0';
        strcat(temp_buffer, "Reason: ");
        if (message) {
            strcat(temp_buffer, message);
        } else {
            strcat(temp_buffer, "No message provided.");
        }
        PrintKernelAt(temp_buffer, line++, margin);

        if (ctx) {
            char hex_buffer[20];
            char dec_buffer[12];
            const int col1 = margin;
            const int col2 = margin + 28;

            // --- CPU Context ---
            PrintKernelAt("- CPU CONTEXT -", line++, col1);
            PrintKernelAt("----------------------", line++, col1);

            U64ToHexStr(ctx->rip, hex_buffer);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RIP:  "); strcat(temp_buffer, hex_buffer);
            PrintKernelAt(temp_buffer, line++, col1);

            U64ToHexStr(ctx->rsp, hex_buffer);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RSP:  "); strcat(temp_buffer, hex_buffer);
            PrintKernelAt(temp_buffer, line++, col1);

            U64ToHexStr(ctx->rbp, hex_buffer);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RBP:  "); strcat(temp_buffer, hex_buffer);
            PrintKernelAt(temp_buffer, line++, col1);

            U64ToHexStr(error_code, hex_buffer);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "CODE: "); strcat(temp_buffer, hex_buffer);
            PrintKernelAt(temp_buffer, line++, col1);

            // --- Source Location ---
            PrintKernelAt("- SOURCE LOCATION -", line++, col1);
            PrintKernelAt("----------------------", line++, col1);

            if (ctx->file && ctx->file[0] != '\0') {
                temp_buffer[0] = '\0'; strcat(temp_buffer, "File: "); strcat(temp_buffer, ctx->file);
                PrintKernelAt(temp_buffer, line++, col1);

                temp_buffer[0] = '\0'; strcat(temp_buffer, "Func: "); strcat(temp_buffer, ctx->function);
                PrintKernelAt(temp_buffer, line++, col1);

                U32ToDecStr(ctx->line, dec_buffer);
                temp_buffer[0] = '\0'; strcat(temp_buffer, "Line: "); strcat(temp_buffer, dec_buffer);
                PrintKernelAt(temp_buffer, line++, col1);
            } else {
                PrintKernelAt("Unavailable", line++, col1);
            }
            PrintKernelAt("- SYSTEM INFO -", line++, col1);
            PrintKernelAt("----------------------", line++, col1);
            PrintKernelAt("\b\b", line++, col1);
        }

        DumpSchedulerState();
        DumpPerformanceStats();

        MemoryStats stats;
        GetDetailedMemoryStats(&stats);
        PrintKernel("  Physical: ");
        PrintKernelInt(stats.free_physical_bytes / (1024*1024));
        PrintKernel("MB free, ");
        PrintKernelInt(stats.fragmentation_score);
        PrintKernel("% fragmented\n");
        PrintVMemStats();
        PrintHeapStats();

        PrintKernelAt("SYSTEM HALTED.", 50, margin);
        ConsoleSetColor(VGA_COLOR_DEFAULT);

        SerialWrite("[FATAL] - [KERNEL PANIC] -- ");
        if (message) SerialWrite(message);
        SerialWrite("\n");
    }

hang:
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

// --- Public Panic Interface Functions ---
// These functions are now perfect. They gather context and pass it to the
// abstract KernelPanicHandler without needing to know anything about the display.

static inline uint64_t __get_rip(void) {
    return (uint64_t)__builtin_return_address(0);
}

void __attribute__((noreturn)) PanicFromInterrupt(const char* message, Registers* regs) {
    asm volatile("cli");
    PanicContext ctx = {0};
    ctx.rip = regs->rip;
    ctx.rsp = regs->rsp;
    ctx.rbp = regs->rbp;
    ctx.error_code = regs->error_code;
    ctx.function = "N/A (From Interrupt)";
    ctx.file     = "";
    ctx.line     = 0;
    KernelPanicHandler(message, regs->error_code, &ctx);
}

void __attribute__((noreturn)) Panic(const char* message) {
    asm volatile("cli");
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    KernelPanicHandler(message, PANIC_GENERAL, &ctx);
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    KernelPanicHandler(message, error_code, &ctx);
}

void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line) {
    asm volatile("cli");
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    ctx.error_code = error_code;
    ctx.function = function;
    ctx.file = file;
    ctx.line = line;
    KernelPanicHandler(message, error_code, &ctx);
}