// SPDX-License-Identifier: GPL-2.0-only
///@brief Unified wrapper for multiple scheduler implementations
#ifndef VOIDFRAME_SCHEDULER_H
#define VOIDFRAME_SCHEDULER_H
#include <MLFQ.h>

#if defined(VF_CONFIG_SCHED_MLFQ)
#include <MLFQ.h>
#elif defined(VF_CONFIG_SCHED_EEVDF)
#include <EEVDF.h>
#endif

#if defined(VF_CONFIG_SCHED_MLFQ)
#define PROC_FLAG_NONE          0U
#define PROC_FLAG_IMMUNE        (1U << 0)
#define PROC_FLAG_CRITICAL      (1U << 1)
#define PROC_FLAG_SUPERVISOR    (1U << 3)
#define PROC_FLAG_CORE          (PROC_FLAG_IMMUNE | PROC_FLAG_SUPERVISOR | PROC_FLAG_CRITICAL)
#elif defined(VF_CONFIG_SCHED_EEVDF)
#define PROC_FLAG_NONE          EEVDF_CAP_NONE
#define PROC_FLAG_IMMUNE        EEVDF_CAP_IMMUNE
#define PROC_FLAG_CRITICAL      EEVDF_CAP_CRITICAL
#define PROC_FLAG_SUPERVISOR    EEVDF_CAP_SUPERVISOR
#define PROC_FLAG_CORE          EEVDF_CAP_CORE
#endif


#if defined(VF_CONFIG_SCHED_MLFQ)
typedef MLFQProcessControlBlock CurrentProcessControlBlock;
#elif defined(VF_CONFIG_SCHED_EEVDF)
typedef EEVDFProcessControlBlock CurrentProcessControlBlock;
#elif defined(VF_CONFIG_SCHED_CFS)
typedef void CurrentProcessControlBlock;
#endif

// Initialize the active scheduler
int SchedulerInit();

// Create a new process
uint32_t CreateProcess(const char * name, void (*entry_point)());

uint32_t CreateSecureProcess(const char * name, void (*entry_point)(), uint8_t priv, uint8_t flag);

CurrentProcessControlBlock* GetCurrentProcess();

CurrentProcessControlBlock* GetCurrentProcessByPID(uint32_t pid);

// Yield CPU
void Yield();

// Main scheduler function (called from interrupt handler)
void Schedule(Registers* regs);

// Kill a process
void KillProcess(uint32_t pid);

void KillCurrentProcess(const char * reason);

void KillAllProcess(const char* reason);

// List processes
void ListProcesses();

// Performande stats
void DumpPerformanceStats();

// Dump scheduler state
void DumpSchedulerState();

uint64_t GetSystemTicks();

#endif