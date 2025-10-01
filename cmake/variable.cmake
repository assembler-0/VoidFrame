# ============================================================================
# VoidFrame Variable Configuration
# ============================================================================
add_compile_definitions(
        MAX_PROCESSES=64
        PAGE_SIZE=4096
        KERNEL_STACK_SIZE=16384
        KERNEL_VIRTUAL_OFFSET=0xFFFFFE0000000000ULL
        VIRT_ADDR_SPACE_LOW_START=0x0000000000001000ULL
        VIRT_ADDR_SPACE_LOW_END=0x00007FFFFFFFFFFFULL
        VIRT_ADDR_SPACE_HIGH_START=0xFFFF800000000000ULL
        VIRT_ADDR_SPACE_HIGH_END=0xFFFFFDFFFFFFFFFFULL
        KERNEL_SPACE_END=0xFFFFFFFFFFFFFFFFULL
)