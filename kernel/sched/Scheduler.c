#include "Scheduler.h"

// Initialize the active scheduler
int SchedulerInit() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQSchedInit();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFSchedInit();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Create a new process
uint32_t CreateProcess(const char * name, void (*entry_point)()) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQCreateProcess (name, entry_point);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFCreateProcess (name, entry_point);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

uint32_t CreateSecureProcess(const char * name, void (*entry_point)(), uint8_t priv, uint8_t flag) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQCreateSecureProcess(name, entry_point, priv, flag);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFCreateSecureProcess(name, entry_point, priv, flag);
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

CurrentProcessControlBlock* GetCurrentProcess() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetCurrentProcess();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetCurrentProcess();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

CurrentProcessControlBlock* GetCurrentProcessByPID(uint32_t pid) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetCurrentProcessByPID(pid);
#elif  defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetCurrentProcessByPID(pid);
#elif  defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}

// Yield CPU
void Yield() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQYield();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFYield();
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

// Main scheduler function (called from interrupt handler)
void Schedule(Registers* regs) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQSchedule(regs);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFSchedule(regs);
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

// Kill a process
void KillProcess(uint32_t pid) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQKillProcess(pid);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFKillProcess(pid);
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

void KillCurrentProcess(const char * reason) {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQKillCurrentProcess(reason);
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFKillCurrentProcess(reason);
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

// List processes
void ListProcesses() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQListProcesses();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFListProcesses();
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

// Performande stats
void DumpPerformanceStats() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQDumpPerformanceStats();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFDumpPerformanceStats();
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

// Dump scheduler state
void DumpSchedulerState() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQDumpSchedulerState();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFDumpSchedulerState();
#elif defined(VF_CONFIG_SCHED_CFS)
    return; // not implemented
#endif
}

uint64_t GetSystemTicks() {
#if defined(VF_CONFIG_SCHED_MLFQ)
    return MLFQGetSystemTicks();
#elif defined(VF_CONFIG_SCHED_EEVDF)
    return EEVDFGetSystemTicks();
#elif defined(VF_CONFIG_SCHED_CFS)
    return 0; // not implemented
#endif
}
