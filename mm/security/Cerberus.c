// mm/security/Cerberus.c
#include "Cerberus.h"
#include "Console.h"
#include "Format.h"
#include "Spinlock.h"
#include "StackGuard.h"
#include "StringOps.h"
#include "VFS.h"

static CerberusState g_cerberus_state = {0};
static volatile int cerberus_lock = 0;
static uint64_t system_ticks = 0;

typedef struct {
    uint64_t base_addr;
    uint64_t end_addr;
    const char* description;
} HardwareAccessRange;

static HardwareAccessRange hardware_ranges[] = {
    {0xCF8, 0xCFF, "PCI Configuration"},
    {0x80, 0x8F, "DMA Controller"},
    {0xA0, 0xBF, "PIC Controller"},
    // Add more hardware ranges as needed
};

static bool IsLegitimateHardwareAccess(uint64_t fault_addr) {
    for (int i = 0; i < sizeof(hardware_ranges)/sizeof(hardware_ranges[0]); i++) {
        if (fault_addr >= hardware_ranges[i].base_addr &&
            fault_addr <= hardware_ranges[i].end_addr) {
            return true;
            }
    }
    return false;
}

void CerberusLogViolation(CerberusViolationReport* report) {
    if (!g_cerberus_state.is_initialized || !report) return;

    SpinLock(&cerberus_lock);

    // Update statistics
    g_cerberus_state.total_violations++;

    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[report->process_id];
    proc_info->violation_count++;
    proc_info->last_violation = system_ticks;

    SpinUnlock(&cerberus_lock);

    // Console logging
    PrintKernelErrorF("[Cerberus] VIOLATION PID=%d: %s\n",
                     report->process_id, report->description);
#ifdef VF_CONFIG_CERBERUS_VFS_LOGGING
    // VFS logging
    char log_entry[256];
    FormatA(log_entry, sizeof(log_entry),
           "TICK=%lu PID=%d TYPE=%d ADDR=0x%lx RIP=0x%lx DESC=%s\n",
           system_ticks, report->process_id, report->violation_type,
           report->fault_address, report->rip, report->description);

    VfsAppendFile("/ProcINFO/Cerberus/violations.log", log_entry, StringLength(log_entry));
#endif

}

void CerberusInit(void) {
    PrintKernel("Cerberus initializing...\n");

    // Initialize all structures
    for (int i = 0; i < CERBERUS_MAX_WATCH_REGIONS; i++) {
        g_cerberus_state.watch_regions[i].is_active = false;
    }

    for (int i = 0; i < CERBERUS_MAX_PROCESSES; i++) {
        g_cerberus_state.process_info[i].is_monitored = false;
        g_cerberus_state.process_info[i].is_compromised = false;
        g_cerberus_state.process_info[i].violation_count = 0;
    }

    g_cerberus_state.active_regions = 0;
    g_cerberus_state.monitored_processes = 0;
    g_cerberus_state.total_violations = 0;
    g_cerberus_state.is_initialized = true;
#ifdef VF_CONFIG_CERBERUS_VFS_LOGGING
    // Create logging directories in VFS
    VfsCreateDir(FormatS("%s/Cerberus", RuntimeServices));
    VfsCreateFile(FormatS("%s/Cerberus/violations.log", RuntimeServices));
    VfsCreateFile(FormatS("%s/Cerberus/watchlist.log", RuntimeServices));
#endif
    PrintKernelSuccess("Cerberus initialized\n");
}

int CerberusRegisterProcess(uint32_t pid, uint64_t stack_base, uint64_t stack_size) {
    if (!g_cerberus_state.is_initialized || pid >= CERBERUS_MAX_PROCESSES) {
        return -1;
    }

    SpinLock(&cerberus_lock);

    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[pid];
    if (proc_info->is_monitored) {
        SpinUnlock(&cerberus_lock);
        return 0; // Already registered
    }

    // Initialize process monitoring
    proc_info->process_id = pid;
    proc_info->is_monitored = true;
    proc_info->is_compromised = false;
    proc_info->violation_count = 0;
    proc_info->last_violation = 0;

    // Install stack protection if stack provided
    // if (stack_base && stack_size > 16) {
    //     CerberusInstallStackCanary(pid, stack_base, stack_size);
    // }

    g_cerberus_state.monitored_processes++;
    SpinUnlock(&cerberus_lock);

    PrintKernelF("[Cerberus] Process %d registered\n", pid);
    return 0;
}

void CerberusUnregisterProcess(uint32_t pid) {
    if (!g_cerberus_state.is_initialized || pid >= CERBERUS_MAX_PROCESSES) {
        return;
    }

    SpinLock(&cerberus_lock);

    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[pid];
    if (proc_info->is_monitored) {
        proc_info->is_monitored = false;
        g_cerberus_state.monitored_processes--;

        // Remove all watch regions for this process
        for (int i = 0; i < CERBERUS_MAX_WATCH_REGIONS; i++) {
            CerberusWatchRegion* region = &g_cerberus_state.watch_regions[i];
            if (region->is_active && region->process_id == pid) {
                region->is_active = false;
                g_cerberus_state.active_regions--;
            }
        }
    }

    SpinUnlock(&cerberus_lock);
    PrintKernelF("[Cerberus] Process %d unregistered\n", pid);
}

int CerberusInstallStackCanary(uint32_t pid, uint64_t stack_base, uint64_t stack_size) {
    if (!g_cerberus_state.is_initialized) return -1;

    // Install canary at end of stack using existing STACK_CANARY_VALUE
    uint64_t canary_addr = stack_base + stack_size - sizeof(uint64_t);
    uint64_t* canary_ptr = (uint64_t*)canary_addr;
    *canary_ptr = STACK_CANARY_VALUE;

    // Record canary location
    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[pid];
    proc_info->stack_canary_addr = canary_addr;

    // Track stack as a watched region
    CerberusTrackAlloc(stack_base, stack_size, pid);

    // Mark as stack region
    for (int i = 0; i < CERBERUS_MAX_WATCH_REGIONS; i++) {
        CerberusWatchRegion* region = &g_cerberus_state.watch_regions[i];
        if (region->is_active && region->base_addr == stack_base && region->process_id == pid) {
            region->is_stack_region = true;
            break;
        }
    }

    return 0;
}

int CerberusCheckStackCanary(uint32_t pid) {
    if (!g_cerberus_state.is_initialized || pid >= CERBERUS_MAX_PROCESSES) return -1;

    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[pid];
    if (!proc_info->is_monitored || !proc_info->stack_canary_addr) return 0;

    uint64_t* canary_ptr = (uint64_t*)proc_info->stack_canary_addr;
    if (*canary_ptr != STACK_CANARY_VALUE) {
        // Stack canary corrupted!
        CerberusViolationReport violation = {
            .violation_type = MEM_VIOLATION_CANARY_CORRUPT,
            .fault_address = proc_info->stack_canary_addr,
            .process_id = pid,
            .rip = 0
        };

        FormatA(violation.description, sizeof(violation.description),
               "Stack canary corrupted: expected=0x%lx found=0x%lx",
               STACK_CANARY_VALUE, *canary_ptr);

        CerberusLogViolation(&violation);
        proc_info->is_compromised = true;
        return 1; // Violation detected
    }

    return 0; // OK
}

void CerberusPreScheduleCheck(uint32_t pid) {
    if (!g_cerberus_state.is_initialized) return;

    CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[pid];
    if (!proc_info->is_monitored) return;

    // Block compromised processes
    if (proc_info->is_compromised) {
        PrintKernelErrorF("[Cerberus] BLOCKED compromised process %d\n", pid);
#ifdef VF_CONFIG_CERBERUS_THREAT_REPORTING
        CerberusReportThreat(pid, MEM_VIOLATION_STACK_CORRUPTION);
#endif
        return;
    }

    // Check stack canary
    if (CerberusCheckStackCanary(pid) != 0) {
        PrintKernelErrorF("[Cerberus] Stack canary violation in PID %d\n", pid);
#ifdef VF_CONFIG_CERBERUS_THREAT_REPORTING
        CerberusReportThreat(pid, MEM_VIOLATION_CANARY_CORRUPT);
#endif
    }

    // Check violation threshold
    if (proc_info->violation_count >= CERBERUS_VIOLATION_THRESHOLD) {
        PrintKernelWarningF("[Cerberus] PID %d exceeded violation threshold\n", pid);
#ifdef VF_CONFIG_CERBERUS_THREAT_REPORTING
        CerberusReportThreat(pid, MEM_VIOLATION_BOUNDS_CHECK);
#endif
    }
}

void CerberusTick(void) {
    if (!g_cerberus_state.is_initialized) return;

    system_ticks++;

    // Periodic security checks
    if (system_ticks % CERBERUS_CHECK_INTERVAL == 0) {
        // Use existing memory leak detection
        CheckResourceLeaks();

        // Check for processes with excessive violations
        for (int i = 0; i < CERBERUS_MAX_PROCESSES; i++) {
            CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[i];
            if (proc_info->is_monitored &&
                proc_info->violation_count > CERBERUS_VIOLATION_THRESHOLD &&
                !proc_info->is_compromised) {

                proc_info->is_compromised = true;
                PrintKernelWarningF("[Cerberus] Process %d marked as compromised\n", i);
            }
        }
    }
}

int CerberusAnalyzeFault(uint64_t fault_addr, uint64_t error_code, uint32_t pid, uint64_t rip) {
    if (!g_cerberus_state.is_initialized) return 0;
    if (IsLegitimateHardwareAccess(fault_addr)) return 0;

    MemorySecurityViolation violation_type = MEM_VIOLATION_NONE;

    // Analyze fault characteristics
    if (fault_addr == 0 || fault_addr < 0x1000) {
        violation_type = MEM_VIOLATION_USE_AFTER_FREE; // NULL pointer access
    } else if ((error_code & 0x2) && !(error_code & 0x1)) {
        violation_type = MEM_VIOLATION_BUFFER_OVERFLOW; // Write to non-present page
    } else if (error_code & 0x10) {
        violation_type = MEM_VIOLATION_STACK_CORRUPTION; // NX bit violation
    } else if (fault_addr >= 0xFFFF800000000000UL) {
        violation_type = MEM_VIOLATION_BOUNDS_CHECK; // Kernel space access
    }

    if (violation_type != MEM_VIOLATION_NONE) {
        CerberusViolationReport violation = {
            .violation_type = violation_type,
            .fault_address = fault_addr,
            .process_id = pid,
            .rip = rip
        };

        FormatA(violation.description, sizeof(violation.description),
               "Memory fault: addr=0x%lx error=0x%lx rip=0x%lx",
               fault_addr, error_code, rip);

        CerberusLogViolation(&violation);
        return 1; // Violation detected
    }

    return 0; // No violation
}

#ifdef VF_CONFIG_CERBERUS_THREAT_REPORTING
void CerberusReportThreat(uint32_t pid, MemorySecurityViolation violation) {
    if (!g_cerberus_state.is_initialized) return;

    // Report to Astra via VFS
    char threat_msg[128];
    FormatA(threat_msg, sizeof(threat_msg),
           "MEMORY_THREAT PID=%d TYPE=%d SEVERITY=%d TICK=%lu",
           pid, violation,
           (violation >= MEM_VIOLATION_STACK_CORRUPTION) ? 3 : 2,
           system_ticks);
    extern char astra_path[1024];
    VfsWriteFile(astra_path, threat_msg, StringLength(threat_msg));

    PrintKernelWarningF("[Cerberus] Threat reported to Astra: PID=%d\n", pid);
}
#endif

int CerberusTrackAlloc(uint64_t addr, uint64_t size, uint32_t pid) {
    if (!g_cerberus_state.is_initialized) return -1;

    SpinLock(&cerberus_lock);

    // Find empty watch region slot
    for (int i = 0; i < CERBERUS_MAX_WATCH_REGIONS; i++) {
        CerberusWatchRegion* region = &g_cerberus_state.watch_regions[i];
        if (!region->is_active) {
            region->base_addr = addr;
            region->size = size;
            region->process_id = pid;
            region->alloc_time = system_ticks;
            region->is_active = true;
            region->is_stack_region = false;

            g_cerberus_state.active_regions++;
            SpinUnlock(&cerberus_lock);
            return 0;
        }
    }

    SpinUnlock(&cerberus_lock);
    return -1; // No space
}

int CerberusTrackFree(uint64_t addr, uint32_t pid) {
    if (!g_cerberus_state.is_initialized) return -1;

    SpinLock(&cerberus_lock);

    // Find and remove watch region
    for (int i = 0; i < CERBERUS_MAX_WATCH_REGIONS; i++) {
        CerberusWatchRegion* region = &g_cerberus_state.watch_regions[i];
        if (region->is_active && region->base_addr == addr && region->process_id == pid) {
            region->is_active = false;
            g_cerberus_state.active_regions--;
            SpinUnlock(&cerberus_lock);
            return 0;
        }
    }

    SpinUnlock(&cerberus_lock);

    // Potential double-free
    CerberusViolationReport violation = {
        .violation_type = MEM_VIOLATION_DOUBLE_FREE,
        .fault_address = addr,
        .process_id = pid,
        .rip = 0
    };

    FormatA(violation.description, sizeof(violation.description),
           "Potential double-free: addr=0x%lx", addr);

    CerberusLogViolation(&violation);
    return 1; // Violation
}

void CerberusDumpStats(void) {
    if (!g_cerberus_state.is_initialized) return;

    PrintKernelF("Status: %s\n", "ACTIVE");
    PrintKernelF("System Ticks: %lu\n", system_ticks);
    PrintKernelF("Monitored Processes: %d\n", g_cerberus_state.monitored_processes);
    PrintKernelF("Watch Regions: %d\n", g_cerberus_state.active_regions);
    PrintKernelF("Total Violations: %d\n", g_cerberus_state.total_violations);

    // Show compromised processes
    PrintKernel("Compromised Processes: ");
    bool found = false;
    for (int i = 0; i < CERBERUS_MAX_PROCESSES; i++) {
        CerberusProcessInfo* proc_info = &g_cerberus_state.process_info[i];
        if (proc_info->is_monitored && proc_info->is_compromised) {
            PrintKernelF("%d ", i);
            found = true;
        }
    }
    if (!found) PrintKernel("None");
    PrintKernel("\n");
}