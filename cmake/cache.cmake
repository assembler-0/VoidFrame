# ============================================================================
# Cache variables
# ============================================================================
set(VF_SCHEDULER "EEVDF" CACHE STRING "Scheduler type: MLFQ or EEVDF")
set_property(CACHE VF_SCHEDULER PROPERTY STRINGS MLFQ EEVDF)

set(CLANG_TARGET_TRIPLE "x86_64-unknown-none-elf" CACHE STRING "Clang target triple for cross-compilation")
set_property(CACHE CLANG_TARGET_TRIPLE PROPERTY STRINGS x86_64-unknown-none-elf x86_64-pc-none-elf x86_64-pc-linux-gnu) # Why would anyone use linux-gnu for a kernel? Who knows.