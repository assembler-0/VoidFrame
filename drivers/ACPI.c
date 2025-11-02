#include <ACPI.h>
#include <Console.h>
#include <Io.h>
#include <MemOps.h>
#include <NVMe.h>
#include <Scheduler.h>
#include <StringOps.h>
#include <VMem.h>
#include <TSC.h>
#include <VFS.h>

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
    uint64_t aligned_addr = phys_addr & ~0xFFF;
    uint64_t offset = phys_addr - aligned_addr;
    uint64_t aligned_size = ((size + offset + 0xFFF) & ~0xFFF);

    void* virt_addr = VMemAlloc(aligned_size);
    if (!virt_addr) {
        PrintKernelError("ACPI: Failed to allocate virtual memory for ACPI table\n");
        return NULL;
    }

    if (VMemUnmap((uint64_t)virt_addr, aligned_size) != VMEM_SUCCESS) {
        VMemFree(virt_addr, aligned_size);
        PrintKernelError("ACPI: Failed to unmap virtual memory for ACPI table\n");
        return NULL;
    }

    if (VMemMapMMIO((uint64_t)virt_addr, aligned_addr, aligned_size, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        // No need to free here as VMemMapMMIO should handle cleanup on failure
        PrintKernelError("ACPI: Failed to map MMIO for ACPI table\n");
        return NULL;
    }

    return (uint8_t*)virt_addr + offset;
}

static ACPIRSDT* g_rsdt = NULL;

void* AcpiFindTable(const char* signature) {
    if (!g_rsdt) {
        return NULL;
    }

    uint32_t entries = (g_rsdt->header.length - sizeof(ACPISDTHeader)) / 4;
    for (uint32_t i = 0; i < entries; i++) {
        ACPISDTHeader* header = (ACPISDTHeader*)MapACPITable(g_rsdt->table_pointers[i], sizeof(ACPISDTHeader));
        if (!header) {
            continue;
        }

        if (FastMemcmp(header->signature, signature, 4) == 0) {
            void* table = MapACPITable(g_rsdt->table_pointers[i], header->length);
            VMemUnmap((uint64_t)header - (g_rsdt->table_pointers[i] & 0xFFF), ((sizeof(ACPISDTHeader) + (g_rsdt->table_pointers[i] & 0xFFF) + 0xFFF) & ~0xFFF));
            return table;
        }

        VMemUnmap((uint64_t)header - (g_rsdt->table_pointers[i] & 0xFFF), ((sizeof(ACPISDTHeader) + (g_rsdt->table_pointers[i] & 0xFFF) + 0xFFF) & ~0xFFF));
    }

    return NULL;
}

bool ACPIInit(void) {
    PrintKernel("ACPI: Initializing ACPI subsystem...\n");

    ACPIRSDPv1* rsdp = FindRSDP();
    if (!rsdp) {
        PrintKernelError("ACPI: RSDP not found\n");
        return false;
    }

    if (!ValidateChecksum(rsdp, sizeof(ACPIRSDPv1))) {
        PrintKernelError("ACPI: Invalid RSDP checksum\n");
        return false;
    }

    g_rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address, sizeof(ACPISDTHeader));
    if (!g_rsdt) {
        PrintKernelError("ACPI: Failed to map RSDT header\n");
        return false;
    }

    if (FastMemcmp(g_rsdt->header.signature, ACPI_RSDT_SIG, 4) != 0) {
        PrintKernelError("ACPI: Invalid RSDT signature\n");
        VMemUnmap((uint64_t)g_rsdt - (rsdp->rsdt_address & 0xFFF), ((sizeof(ACPISDTHeader) + (rsdp->rsdt_address & 0xFFF) + 0xFFF) & ~0xFFF));
        return false;
    }

    uint32_t rsdt_size = g_rsdt->header.length;
    VMemUnmap((uint64_t)g_rsdt - (rsdp->rsdt_address & 0xFFF), ((sizeof(ACPISDTHeader) + (rsdp->rsdt_address & 0xFFF) + 0xFFF) & ~0xFFF));
    g_rsdt = (ACPIRSDT*)MapACPITable(rsdp->rsdt_address, rsdt_size);
    if (!g_rsdt) {
        PrintKernelError("ACPI: Failed to map full RSDT\n");
        return false;
    }

    g_fadt = (ACPIFADT*)AcpiFindTable(ACPI_FADT_SIG);
    if (!g_fadt) {
        PrintKernelError("ACPI: FADT not found or invalid\n");
        return false;
    }

    g_acpi_initialized = true;
    PrintKernelSuccess("ACPI: Subsystem initialized\n");
    return true;
}

void ACPIResetProcedure() {
    PrintKernel("ACPI: Unmounting Filesystems...\n");
    VfsUnmountAll();
    PrintKernelSuccess("ACPI: Filesystems unmounted\n");

    PrintKernel("ACPI: Stopping all processes and services...\n");
    KillAllProcess("SHUTDOWN");
    PrintKernelSuccess("ACPI: All processes and services stopped\n");

    PrintKernel("ACPI: Stopping NVMe driver...\n");
    NVMe_Shutdown();
    PrintKernelSuccess("ACPI: NVMe driver stopped\n");
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