# ============================================================================
# Cache variables
# ============================================================================
set(VF_SCHEDULER "EEVDF" CACHE STRING "Scheduler type: MLFQ or EEVDF")
set_property(CACHE VF_SCHEDULER PROPERTY STRINGS MLFQ EEVDF)

set(VF_CONFIG_HEAP_LANG "C" CACHE STRING "Heap language: RUST or C")
set_property(CACHE VF_CONFIG_HEAP_LANG PROPERTY STRINGS RUST C)
