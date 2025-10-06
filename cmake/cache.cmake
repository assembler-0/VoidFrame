# ============================================================================
# Cache variables
# ============================================================================
set(VF_SCHEDULER "EEVDF" CACHE STRING "Scheduler type: MLFQ or EEVDF")
set_property(CACHE VF_SCHEDULER PROPERTY STRINGS MLFQ EEVDF)

set(VF_CONFIG_HEAP_RUST ON CACHE BOOL "Use Rust (and C) for heap management")
