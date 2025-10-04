# ============================================================================
# Cache variables
# ============================================================================
set(VF_SCHEDULER "MLFQ" CACHE STRING "Scheduler type: MLFQ or EEVDF")
set_property(CACHE VF_SCHEDULER PROPERTY STRINGS MLFQ EEVDF)
