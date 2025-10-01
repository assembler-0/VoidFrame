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
        VF_CONFIG_SCHED_MLFQ
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
elseif(AUTOMATIC_POST)
    add_compile_definitions(VF_CONFIG_AUTOMATIC_POST)
endif()