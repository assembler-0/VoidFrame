// SPDX-License-Identifier: GPL-2.0-only
///@brief Unified wrapper for multiple scheduler implementations
#ifndef VOIDFRAME_SCHEDULER_H
#define VOIDFRAME_SCHEDULER_H
#include "MLFQ.h"

static inline __attribute__((always_inline)) int SchedulerInit() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQSchedInit();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return 0; // not implemented
#endif
    return -1;
}

static inline __attribute__((always_inline)) uint32_t CreateProcess(const char * name, void (*entry_point)()) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQCreateProcess(name ,entry_point);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return 0; // not implemented
#endif
    return -1;
}

static inline __attribute__((always_inline))
#if defined(VF_CONFIG_SCHED_MLFQ)
MLFQProcessControlBlock*
#elif defined(VF_CONFIG_SCHED_CFS)
void // not implemented
#else
void
#endif
GetCurrentProcess() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQGetCurrentProcess();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

static inline __attribute__((always_inline))
#if defined(VF_CONFIG_SCHED_MLFQ)
MLFQProcessControlBlock*
#elif defined(VF_CONFIG_SCHED_CFS)
void // not implemented
#else
void
#endif
GetProcessByPID(uint32_t pid) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQGetCurrentProcessByPID(pid);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

static inline __attribute__((always_inline)) void Yield() {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQYield();
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

static inline __attribute__((always_inline)) void Schedule(Registers* regs) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQSchedule(regs);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

static inline __attribute__((always_inline)) void KillProcess(uint32_t pid) {
#ifdef VF_CONFIG_SCHED_MLFQ
    return MLFQKillProcess(pid);
#endif
#ifdef VF_CONFIG_SCHED_CFS
    return; // not implemented
#endif
}

#endif // VOIDFRAME_SCHEDULER_H
