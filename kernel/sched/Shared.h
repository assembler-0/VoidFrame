#ifndef VF_SCHED_SHARED_H
#define VF_SCHED_SHARED_H

#define PROC_PRIV_SYSTEM     0  // Highest privilege (kernel services)
#define PROC_PRIV_NORM       1  // Normal processes
#define PROC_PRIV_RESTRICTED 2  // Restricted processes

// Security flags
#define PROC_FLAG_NONE          0U
#define PROC_FLAG_IMMUNE        (1U << 0)
#define PROC_FLAG_CRITICAL      (1U << 1)
#define PROC_FLAG_SUPERVISOR    (1U << 3)
#define PROC_FLAG_CORE          (PROC_FLAG_IMMUNE | PROC_FLAG_SUPERVISOR | PROC_FLAG_CRITICAL)

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