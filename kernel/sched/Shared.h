#ifndef VF_SCHED_SHARED_H
#define VF_SCHED_SHARED_H

#define PROC_PRIV_SYSTEM     0  // Highest privilege (kernel services)
#define PROC_PRIV_NORM       1  // Normal processes
#define PROC_PRIV_RESTRICTED 2  // Restricted processes

typedef enum {
    PROC_TERMINATED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
    PROC_DYING
} ProcessState;

typedef enum {
    TERM_NORMAL = 0,
    TERM_KILLED,
    TERM_CRASHED,
    TERM_SECURITY,
    TERM_RESOURCE
} TerminationReason;

#endif