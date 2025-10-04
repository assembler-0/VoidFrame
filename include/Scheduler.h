// SPDX-License-Identifier: GPL-2.0-only
///@brief Unified wrapper for multiple scheduler implementations
#ifndef VOIDFRAME_SCHEDULER_H
#define VOIDFRAME_SCHEDULER_H
#include "MLFQ.h"
#include "EEVDF.h"
#include "Shared.h"

// Initialize the active scheduler
static inline __attribute__((always_inline)) int SchedulerInit() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQSchedInit();
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFSchedInit();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return 0; // not implemented
#endif
    return -1;
}

// Create a new process
static inline __attribute__((always_inline)) uint32_t CreateProcess(const char * name, void (*entry_point)()) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQCreateProcess(name ,entry_point);
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFCreateProcess(name, entry_point);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return 0; // not implemented
#endif
    return -1;
}

static inline __attribute__((always_inline))
#ifdef VF_CONFIG_SCHED_MLFQ
MLFQProcessControlBlock *
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
EEVDFProcessControlBlock *
#endif
#ifdef VF_CONFIG_SCHED_CFS
void *
#endif
GetCurrentProcess() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQGetCurrentProcess();
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFGetCurrentProcess();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return NULL;
#endif
}

// Yield CPU
static inline __attribute__((always_inline)) void Yield() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQYield();
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFYield();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

// Main scheduler function (called from interrupt handler)
static inline __attribute__((always_inline)) void Schedule(Registers* regs) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQSchedule(regs);
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFSchedule(regs);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

// Kill a process
static inline __attribute__((always_inline)) void KillProcess(uint32_t pid) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQKillProcess(pid);
#endif
#ifdef VF_CONFIG_SCHED_EEVDF
    return EEVDFKillProcess(pid);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

// Scheduler-specific PCB access (for when you need the full scheduler-specific data)
#ifdef VF_CONFIG_SCHED_MLFQ
typedef MLFQProcessControlBlock SchedulerSpecificPCB;
static inline MLFQProcessControlBlock* GetMLFQCurrentProcess() {
    return MLFQGetCurrentProcess();
}
static inline MLFQProcessControlBlock* GetMLFQProcessByPID(uint32_t pid) {
    return MLFQGetCurrentProcessByPID(pid);
}
#endif

#ifdef VF_CONFIG_SCHED_EEVDF
typedef EEVDFProcessControlBlock SchedulerSpecificPCB;
static inline EEVDFProcessControlBlock* GetEEVDFCurrentProcess() {
    return EEVDFGetCurrentProcess();
}
static inline EEVDFProcessControlBlock* GetEEVDFProcessByPID(uint32_t pid) {
    return EEVDFGetCurrentProcessByPID(pid);
}
#endif

#endif // VOIDFRAME_SCHEDULER_H
