// mm/security/Cerberus.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <StackTrace.h>

// Configuration
#define CERBERUS_MAX_WATCH_REGIONS 64
#define CERBERUS_MAX_PROCESSES 128
#define CERBERUS_VIOLATION_THRESHOLD 3
#define CERBERUS_CHECK_INTERVAL 50

// Memory security violation types
typedef enum {
    MEM_VIOLATION_NONE = 0,
    MEM_VIOLATION_BUFFER_OVERFLOW,
    MEM_VIOLATION_STACK_CORRUPTION,
    MEM_VIOLATION_USE_AFTER_FREE,
    MEM_VIOLATION_DOUBLE_FREE,
    MEM_VIOLATION_BOUNDS_CHECK,
    MEM_VIOLATION_CANARY_CORRUPT,
    MEM_VIOLATION_HEAP_CORRUPT
} MemorySecurityViolation;

typedef enum {
    CERBERUS_THREAT_MEMORY = 100,    // Memory violations
    CERBERUS_THREAT_STACK,           // Stack corruption
    CERBERUS_THREAT_CANARY,          // Canary violations
    CERBERUS_THREAT_EXCESSIVE        // Excessive violations
} CerberusThreatType;

typedef struct {
    uint32_t pid;
    MemorySecurityViolation violation_type;
    uint64_t fault_address;
    uint64_t rip;
    uint32_t severity;
    uint64_t timestamp;
} CerberusThreatReport;

typedef struct {
    uint64_t base_addr;
    uint64_t size;
    uint32_t process_id;
    uint64_t alloc_time;
    bool is_active;
    bool is_stack_region;
} CerberusWatchRegion;

typedef struct {
    uint32_t process_id;
    uint32_t violation_count;
    uint64_t last_violation;
    uint64_t stack_canary_addr;
    bool is_monitored;
    bool is_compromised;
} CerberusProcessInfo;

typedef struct {
    MemorySecurityViolation violation_type;
    uint64_t fault_address;
    uint32_t process_id;
    uint64_t rip;
    char description[128];
} CerberusViolationReport;

// Global state
typedef struct {
    CerberusWatchRegion watch_regions[CERBERUS_MAX_WATCH_REGIONS];
    CerberusProcessInfo process_info[CERBERUS_MAX_PROCESSES];
    uint32_t active_regions;
    uint32_t monitored_processes;
    uint32_t total_violations;
    bool is_initialized;
} CerberusState;

// Public API
void CerberusInit(void);
int CerberusRegisterProcess(uint32_t pid, uint64_t stack_base, uint64_t stack_size);
void CerberusUnregisterProcess(uint32_t pid);
void CerberusPreScheduleCheck(uint32_t pid);
void CerberusTick(void);
int CerberusAnalyzeFault(uint64_t fault_addr, uint64_t error_code, uint32_t pid, uint64_t rip);
#ifdef VF_CONFIG_CERBERUS_THREAT_REPORTING
void CerberusReportThreat(uint32_t pid, MemorySecurityViolation violation);
#endif
void CerberusDumpStats(void);

// Memory tracking
int CerberusTrackAlloc(uint64_t addr, uint64_t size, uint32_t pid);
int CerberusTrackFree(uint64_t addr, uint32_t pid);

// Stack protection
int CerberusInstallStackCanary(uint32_t pid, uint64_t stack_base, uint64_t stack_size);
int CerberusCheckStackCanary(uint32_t pid);