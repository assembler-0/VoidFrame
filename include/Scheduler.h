// SPDX-License-Identifier: GPL-2.0-only
///@brief Unified wrapper for multiple scheduler implementations
#ifndef VOIDFRAME_SCHEDULER_H
#define VOIDFRAME_SCHEDULER_H

#if defined(VF_CONFIG_SCHED_MLFQ)
#include "MLFQ.h"
#elif defined(VF_CONFIG_SCHED_EEVDF)
#include "EEVDF.h"
#endif

#if defined(VF_CONFIG_SCHED_MLFQ)
typedef MLFQProcessControlBlock CurrentProcessControlBlock;
#elif defined(VF_CONFIG_SCHED_EEVDF)
typedef EEVDFProcessControlBlock CurrentProcessControlBlock;
#elif defined(VF_CONFIG_SCHED_CFS)
typedef void CurrentProcessControlBlock;
#endif

// Initialize the active scheduler
static inline __attribute__((always_inline)) int SchedulerInit() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQSchedInit();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFSchedInit();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Create a new process
static inline __attribute__((always_inline)) uint32_t CreateProcess(const char * name, void (*entry_point)()) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQCreateProcess (name, entry_point);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFCreateProcess (name, entry_point);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

static inline __attribute__((always_inline)) CurrentProcessControlBlock* GetCurrentProcess() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetCurrentProcess();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetCurrentProcess();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

static inline __attribute__((always_inline)) CurrentProcessControlBlock* GetCurrentProcessByPID(uint32_t pid) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetCurrentProcessByPID(pid);
#elif  defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetCurrentProcessByPID(pid);
#elif  defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Yield CPU
static inline __attribute__((always_inline)) void Yield() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQYield();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFYield();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Main scheduler function (called from interrupt handler)
static inline __attribute__((always_inline)) void Schedule(Registers* regs) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQSchedule(regs);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFSchedule(regs);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Kill a process
static inline __attribute__((always_inline)) void KillProcess(uint32_t pid) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQKillProcess(pid);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFKillProcess(pid);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

static inline __attribute__((always_inline)) void KillCurrentProcess(const char * reason) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQKillCurrentProcess(reason);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFKillCurrentProcess(reason);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// List processes
static inline __attribute__((always_inline)) void ListProcesses() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQListProcesses();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFListProcesses();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Performande stats
static inline __attribute__((always_inline)) void DumpPerformanceStats() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQDumpPerformanceStats();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFDumpPerformanceStats();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Dump scheduler state
static inline __attribute__((always_inline)) void DumpSchedulerState() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQDumpSchedulerState();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFDumpSchedulerState();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

static inline __attribute__((always_inline)) uint64_t GetSystemTicks() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetSystemTicks();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetSystemTicks();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}


#endif // VOIDFRAME_SCHEDULER_H
