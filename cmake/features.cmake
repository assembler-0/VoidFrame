# ============================================================================
# VoidFrame Feature Configuration Flags
# ============================================================================
add_compile_definitions(
        VF_CONFIG_ENABLE_XHCI
        VF_CONFIG_ENABLE_VIRTIO
        VF_CONFIG_ENABLE_ISA
        VF_CONFIG_ENABLE_LPT
        VF_CONFIG_ENABLE_PCI
        VF_CONFIG_ENABLE_PS2
        VF_CONFIG_ENABLE_IDE
        VF_CONFIG_ENABLE_VFCOMPOSITOR
        VF_CONFIG_ENABLE_AHCI
#        VF_CONFIG_ENABLE_NVME
        VF_CONFIG_ENABLE_GENERIC_SOUND
        VF_CONFIG_RTC_CENTURY
        VF_CONFIG_LOAD_MB_MODULES
        VF_CONFIG_ENFORCE_MEMORY_PROTECTION
        VF_CONFIG_VM_HOST
        VF_CONFIG_SNOOZE_ON_BOOT
        VF_CONFIG_PROCINFO_CREATE_DEFAULT
        VF_CONFIG_USE_VFSHELL
        VF_CONFIG_USE_DYNAMOX
        VF_CONFIG_USE_ASTRA
        VF_CONFIG_USE_CERBERUS
        VF_CONFIG_CERBERUS_STACK_PROTECTION
        VF_CONFIG_INTEL
)

add_compile_definitions(
        #    VF_CONFIG_ENABLE_VMWARE_SVGA_II
        #    VF_CONFIG_ENABLE_CERBERUS_VFS_LOGGING
        #    VF_CONFIG_CERBERUS_THREAT_REPORTING
        #    VF_CONFIG_PROCINFO_AUTO_CLEANUP
        #    VF_CONFIG_PANIC_OVERRIDE
)

if(EXCLUDE_EXTRA_OBJECTS)
    add_compile_definitions(VF_CONFIG_EXCLUDE_EXTRA_OBJECTS)
endif()

if(AUTOMATIC_POST)
    add_compile_definitions(VF_CONFIG_AUTOMATIC_POST)
endif()

if(VF_SCHEDULER STREQUAL "MLFQ")
    add_compile_definitions(VF_CONFIG_SCHED_MLFQ)
elseif(VF_SCHEDULER STREQUAL "EEVDF")
    add_compile_definitions(VF_CONFIG_SCHED_EEVDF)
else()
    message(FATAL_ERROR "Invalid scheduler: ${VF_SCHEDULER}. Did you pass: -DVF_SCHEDULER=<MLFQ/EEVDF>?")
endif()

if(VF_CONFIG_HEAP_LANG STREQUAL "RUST")
    add_compile_definitions(VF_CONFIG_HEAP_RUST)
elseif(VF_CONFIG_HEAP_LANG STREQUAL "C")
    add_compile_definitions(VF_CONFIG_HEAP_C)
else()
    message(FATAL_ERROR "Invalid heap language: ${VF_CONFIG_HEAP_LANG}. Did you pass: -DVF_CONFIG_HEAP_LANG=<RUST/C>?")
endif()