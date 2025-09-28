#include "ACPI.h"
#include "Console.h"
#include "Io.h"
#include "MemOps.h"
#include "StringOps.h"
#include "VMem.h"
#include "TSC.h"

static ACPIFADT* g_fadt = NULL;
static bool g_acpi_initialized = false;

// Find RSDP in BIOS memory areas
static ACPIRSDPv1* FindRSDP(void) {
    // Search in EBDA (Extended BIOS Data Area)
    uint16_t ebda = *(uint16_t*)0x40E;
    if (ebda) {
        uint8_t* ebda_ptr = (uint8_t*)(ebda << 4);
        for (uint32_t i = 0; i < 1024; i += 16) {
            if (FastMemcmp(ebda_ptr + i, ACPI_RSDP_SIG, 8) == 0) {
                return (ACPIRSDPv1*)(ebda_ptr + i);
            }
        }
    }
    
    // Search in BIOS ROM area (0xE0000-0xFFFFF)
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (FastMemcmp((void*)addr, ACPI_RSDP_SIG, 8) == 0) {
            return (ACPIRSDPv1*)addr;
        }
    }
    
    return NULL;
}

// Validate ACPI table checksum
static bool ValidateChecksum(void* table, uint32_t length) {
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)table;
    for (uint32_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

// Map ACPI table to virtual memory
static void* MapACPITable(uint32_t phys_addr, uint32_t size) {
    // Align to page boundaries
    uint32_t aligned_addr = phys_addr & ~0xFFF;
    uint32_t offset = phys_addr - aligned_addr;
    uint32_t aligned_size = ((size + offset + 0xFFF) & ~0xFFF);
    
    void* virt_addr = VMemAlloc(aligned_size);
    if (!virt_addr) return NULL;
    
    if (VMemUnmap((uint64_t)virt_addr, aligned_size) != VMEM_SUCCESS) {
        VMemFree(virt_addr, aligned_size);
        return NULL;
    }
    
    if (VMemMapMMIO((uint64_t)virt_addr, aligned_addr, aligned_size, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        return NULL;
    }
    
    return (uint8_t*)virt_addr + offset;
}

bool ACPIInit(void) {
    PrintKernel("ACPI: Initializing ACPI subsystem...\n");
    
    // Find RSDP
    ACPIRSDPv1* rsdp = FindRSDP();
    if (!rsdp) {
        PrintKernelError("ACPI: RSDP not found\n");
        return false;
    }
    
    PrintKernel("ACPI: Found RSDP at 0x");
    PrintKernelHex((uint64_t)rsdp);
    PrintKernel("\n");
    
    // Validate RSDP checksum
    if (!ValidateChecksum(rsdp, sizeof(ACPIRSDPv1))) {
        PrintKernelError("ACPI: Invalid RSDP checksum\n");
        return false;
    }
    
    // Map RSDT
    ACPIRSDT* rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address, sizeof(ACPISDTHeader));
    if (!rsdt) {
        PrintKernelError("ACPI: Failed to map RSDT\n");
        return false;
    }
    
    // Validate RSDT
    if (FastMemcmp(rsdt->header.signature, ACPI_RSDT_SIG, 4) != 0) {
        PrintKernelError("ACPI: Invalid RSDT signature\n");
        return false;
    }
    
    // Remap RSDT with correct size
    uint32_t rsdt_size = rsdt->header.length;
    rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address, rsdt_size);
    if (!rsdt) {
        PrintKernelError("ACPI: Failed to remap RSDT\n");
        return false;
    }
    
    PrintKernel("ACPI: RSDT mapped, length=");
    PrintKernelInt(rsdt_size);
    PrintKernel("\n");
    
    // Find FADT
    uint32_t entries = (rsdt_size - sizeof(ACPISDTHeader)) / 4;
    for (uint32_t i = 0; i < entries; i++) {
        ACPISDTHeader* header = (ACPISDTHeader*)MapACPITable(rsdt->table_pointers[i], sizeof(ACPISDTHeader));
        if (!header) continue;
        
        if (FastMemcmp(header->signature, ACPI_FADT_SIG, 4) == 0) {
            PrintKernel("ACPI: Found FADT\n");
            g_fadt = (ACPIFADT*)MapACPITable(rsdt->table_pointers[i], header->length);
            if (g_fadt && ValidateChecksum(g_fadt, g_fadt->header.length)) {
                PrintKernel("ACPI: FADT validated, PM1A_CNT=0x");
                PrintKernelHex(g_fadt->pm1a_control_block);
                PrintKernel("\n");
                g_acpi_initialized = true;
                return true;
            }
        }
    }
    
    PrintKernelError("ACPI: FADT not found or invalid\n");
    return false;
}

void ACPIShutdown(void) {
    if (!g_acpi_initialized || !g_fadt) {
        PrintKernel("ACPI: Shutdown not available, using fallback\n");
        outw(0x604, 0x2000);
        return;
    }
    
    PrintKernel("ACPI: Initiating shutdown...\n");
    
    // Enable ACPI mode if needed
    if (g_fadt->smi_command_port && g_fadt->acpi_enable) {
        PrintKernel("ACPI: Enabling ACPI mode via SMI\n");
        outb(g_fadt->smi_command_port, g_fadt->acpi_enable);
    }
    
    // Try multiple shutdown methods
    uint16_t shutdown_values[] = {0x2000, 0x3C00, 0x1400, 0x0000};
    
    for (int i = 0; i < 4; i++) {
        PrintKernel("ACPI: Trying shutdown value 0x");
        PrintKernelHex(shutdown_values[i]);
        PrintKernel(" on port 0x");
        PrintKernelHex(g_fadt->pm1a_control_block);
        PrintKernel("\n");
        
        outw(g_fadt->pm1a_control_block, shutdown_values[i]);
        
        // Wait a bit
        for (volatile int j = 0; j < 1000000; j++);
    }
    
    // Fallback methods
    PrintKernel("ACPI: Trying QEMU shutdown\n");
    outw(0x604, 0x2000);
    
    PrintKernel("ACPI: Trying Bochs shutdown\n");
    outw(0xB004, 0x2000);
    
    PrintKernelError("ACPI: All shutdown methods failed\n");
}

void ACPIReboot(void) {
    PrintKernel("ACPI: Initiating reboot...\n");
    
    // Try keyboard controller reset
    outb(0x64, 0xFE);

    PrintKernel("ACPI: falling back to triple faulting...\n");

    // Fallback: triple fault
    __asm__ volatile("cli; hlt");
}