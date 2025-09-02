#ifndef VF_SCHED_SHARED_H
#define VF_SCHED_SHARED_H

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