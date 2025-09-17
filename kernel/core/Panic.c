#include "Panic.h"
#include "Io.h"
// The ONLY necessary includes for display are now Console and Serial
#include "../../mm/KernelHeap.h"
#include "../../mm/PMem.h"
#include "Console.h"
#include "Serial.h"

#include "../../mm/MemOps.h"
#include "../../mm/VMem.h"
#include "MLFQ.h" // For Registers struct in PanicFromInterrupt
#include "Vesa.h"
#include "stdint.h"
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
    cli();
    ClearScreen();
    ConsoleSetColor(VGA_COLOR_WHITE);
    Unsnooze();
    // Reentrancy guard to prevent recursive panics
    static volatile int in_panic = 0;
    if (__atomic_exchange_n(&in_panic, 1, __ATOMIC_ACQ_REL)) {
        goto hang;
    }

    // Determine display mode ONCE at the beginning
    int use_vbe_graphics = VBEIsInitialized();

    if (use_vbe_graphics) {
#ifndef VF_CONFIG_EXCLUDE_EXTRA_OBJECTS
        VBEShowPanic();
#endif
        PrintKernel("[FATAL] - [KERNEL PANIC] - ");
        if (message) PrintKernel(message);
        else PrintKernel("No message provided.");
        PrintKernel("\n");
        if (ctx) {
            char hex[20];
            char dec_buffer[12];
            char temp_buffer[128];
            temp_buffer[0] = '\0';
            PrintKernel("[CPU CONTEXT]\n");
            PrintKernel("----------------------\n");

            U64ToHexStr(ctx->rip, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RIP:  "); strcat(temp_buffer, hex);
            PrintKernel(temp_buffer);
            PrintKernel("\n");

            U64ToHexStr(ctx->rsp, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RSP:  "); strcat(temp_buffer, hex);
            PrintKernel(temp_buffer);
            PrintKernel("\n");

            U64ToHexStr(ctx->rbp, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "RBP:  "); strcat(temp_buffer, hex);
            PrintKernel(temp_buffer);
            PrintKernel("\n");

            U64ToHexStr(error_code, hex);
            temp_buffer[0] = '\0'; strcat(temp_buffer, "CODE: "); strcat(temp_buffer, hex);
            PrintKernel(temp_buffer);
            PrintKernel("\n");

            // --- Source Location ---
            PrintKernel("[SOURCE LOCATION]\n");
            PrintKernel("----------------------\n");

            if (ctx->file && ctx->file[0] != '\0') {
                temp_buffer[0] = '\0'; strcat(temp_buffer, "File: "); strcat(temp_buffer, ctx->file);
                PrintKernel(temp_buffer);
                PrintKernel("\n");

                temp_buffer[0] = '\0'; strcat(temp_buffer, "Func: "); strcat(temp_buffer, ctx->function);
                PrintKernel(temp_buffer);
                PrintKernel("\n");

                U32ToDecStr(ctx->line, dec_buffer);
                temp_buffer[0] = '\0'; strcat(temp_buffer, "Line: "); strcat(temp_buffer, dec_buffer);
                PrintKernel(temp_buffer);
                PrintKernel("\n");
            } else {
                PrintKernel("Unavailable\n");
            }
            PrintKernel("[SYSTEM INFORMATION]\n");
            PrintKernel("----------------------\n");
            MLFQDumpSchedulerState();

            MemoryStats stats;
            GetDetailedMemoryStats(&stats);
            PrintKernel("  Physical: ");
            PrintKernelInt(stats.free_physical_bytes / (1024*1024));
            PrintKernel("MB free, ");
            PrintKernelInt(stats.fragmentation_score);
            PrintKernel("% fragmented\n");
            PrintVMemStats();
            PrintHeapStats();

            PrintKernelAt("SYSTEM HALTED.", 50, 45);
            ConsoleSetColor(VGA_COLOR_DEFAULT);
        }
    } else {
        // Pure text mode path - no graphics
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

        MLFQDumpSchedulerState();
        MLFQDumpPerformanceStats();

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
   cli();
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
   cli();
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    __asm__ volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    __asm__ volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    KernelPanicHandler(message, PANIC_GENERAL, &ctx);
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
   cli();
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    __asm__ volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    __asm__ volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    KernelPanicHandler(message, error_code, &ctx);
}

void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line) {
   cli();
    PanicContext ctx = {0};
    ctx.rip = __get_rip();
    __asm__ volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    __asm__ volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    ctx.error_code = error_code;
    ctx.function = function;
    ctx.file = file;
    ctx.line = line;
    KernelPanicHandler(message, error_code, &ctx);
}