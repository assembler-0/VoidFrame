#include "ACPI.h"
#include "Console.h"
#include "Io.h"
#include "MemOps.h"
#include "Scheduler.h"
#include "StringOps.h"
#include "VMem.h"
#include "TSC.h"
#include "VFS.h"

static ACPIFADT* g_fadt = NULL;
static bool g_acpi_initialized = false;

// Find RSDP in BIOS memory areas
static ACPIRSDPv1* FindRSDP(void) {
    // Search in EBDA (Extended BIOS Data Area)
    uint16_t ebda = 0;
    if (VMemGetPhysAddr(0x40E) != 0) {
        ebda = *(uint16_t*)0x40E;
    }
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
        VMemFree(virt_addr, aligned_size);
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

    ACPIRSDT* rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address,
                                             sizeof(ACPISDTHeader));
    if (!rsdt) {
        PrintKernelError("ACPI: Failed to map RSDT\n");
        return false;
    }
    // Validate RSDT signature
    if (FastMemcmp(rsdt->header.signature, ACPI_RSDT_SIG, 4) != 0) {
        PrintKernelError("ACPI: Invalid RSDT signature\n");
        // Clean up the initial header‐only mapping
        VMemUnmap((uint64_t)rsdt - (rsdp->rsdt_address & 0xFFF),
                  ((sizeof(ACPISDTHeader) + (rsdp->rsdt_address & 0xFFF) + 0xFFF)
                   & ~0xFFF));
        return false;
    }
    // Remap with the full table length
    uint32_t rsdt_size = rsdt->header.length;
    // Remember the old header mapping so we can free it afterward
    void*    old_rsdt   = rsdt;
    uint32_t old_offset = rsdp->rsdt_address & 0xFFF;
    rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address, rsdt_size);
    // Now unmap the temporary header‐only region
    VMemUnmap((uint64_t)old_rsdt - old_offset,
              ((sizeof(ACPISDTHeader) + old_offset + 0xFFF) & ~0xFFF));
    
    PrintKernel("ACPI: RSDT mapped, length=");
    PrintKernelInt(rsdt_size);
    PrintKernel("\n");
    
    // Find FADT
    uint32_t entries = (rsdt_size - sizeof(ACPISDTHeader)) / 4;
    for (uint32_t i = 0; i < entries; i++) {
        ACPISDTHeader* header = (ACPISDTHeader*)MapACPITable(
            rsdt->table_pointers[i],
            sizeof(ACPISDTHeader)
        );
        if (!header) continue;

        bool is_fadt = FastMemcmp(header->signature, ACPI_FADT_SIG, 4) == 0;
        uint32_t table_length = header->length;
        uint32_t header_offset = rsdt->table_pointers[i] & 0xFFF;

        // Unmap the header mapping now that we've inspected it
        VMemUnmap(
            (uint64_t)header - header_offset,
            ( (sizeof(ACPISDTHeader) + header_offset + 0xFFF) & ~0xFFF )
        );

        if (is_fadt) {
            PrintKernel("ACPI: Found FADT\n");
            g_fadt = (ACPIFADT*)MapACPITable(
                rsdt->table_pointers[i],
                table_length
            );
            break;
        }
    }
    
    PrintKernelError("ACPI: FADT not found or invalid\n");
    return false;
}

void ACPIResetProcedure() {
    PrintKernel("ACPI: Unmounting Filesystems...\n");
    VfsUnmountAll();
    PrintKernelSuccess("ACPI: Filesystems unmounted\n");

    PrintKernel("ACPI: Stopping all processes and services...\n");
    KillAllProcess("SHUTDOWN");
    PrintKernelSuccess("ACPI: All processes and services stopped\n");
}

void ACPIShutdown(void) {
    PrintKernel("ACPI: Initiating shutdown...\n");
    
    // Enable ACPI mode if needed
    if (g_fadt->smi_command_port && g_fadt->acpi_enable) {
        PrintKernel("ACPI: Enabling ACPI mode via SMI\n");
        outb(g_fadt->smi_command_port, g_fadt->acpi_enable);
    }

    ACPIResetProcedure();

    // Try multiple shutdown methods
    for (int i = 0; i < 4; i++) {
        const uint16_t shutdown_values[] = {0x2000, 0x3C00, 0x1400, 0x0000};
        PrintKernel("ACPI: Trying shutdown value 0x");
        PrintKernelHex(shutdown_values[i]);
        PrintKernel(" on port 0x");
        PrintKernelHex(g_fadt->pm1a_control_block);
        PrintKernel("\n");
        
        outw(g_fadt->pm1a_control_block, shutdown_values[i]);
        
        // Wait a bit
        delay(10);
    }

    PrintKernel("ACPI: Trying Bochs shutdown\n");
    outw(0xB004, 0x2000);
    
    PrintKernelError("ACPI: All shutdown methods failed\n");
}

void ACPIReboot(void) {

    ACPIResetProcedure();

    PrintKernel("ACPI: Initiating reboot...\n");
    
    // Try keyboard controller reset
    outb(0x64, 0xFE);

    PrintKernel("ACPI: falling back to triple faulting...\n");

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) invalid_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int $0x03" :: "m"(invalid_idt));
    __builtin_unreachable();
}