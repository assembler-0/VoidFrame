# ============================================================================
# VoidFrame CMake Build Script - v0.0.2-development3
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(VoidFrame VERSION 0.0.2 LANGUAGES C CXX ASM_NASM)
enable_language(ASM_NASM)

# ============================================================================
# Platform Checks
# ============================================================================
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Detected Linux host system: ${CMAKE_HOST_SYSTEM_NAME}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "Windows detected. Install WSL or use a cross-compiler.")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(WARNING "macOS detected. Please make sure you have compatible tools.")
else()
    message(FATAL_ERROR "Unsupported host system: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

# ============================================================================
# Standard Configuration
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Module Path & Includes
# ============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(features)
include(variable)
include(dependencies)
include(flags)
include(configuration)
include(run)
include(clang-x64)

# ============================================================================ 
# Source Files Organization
# ============================================================================ 
set(ASM_SOURCES
    arch/x86_64/asm/pxs.asm
    arch/x86_64/idt/IdtLoad.asm
    arch/x86_64/gdt/GdtTssFlush.asm
    arch/x86_64/interrupts/Interrupts.asm
    arch/x86_64/syscall/SyscallEntry.asm
    include/Switch.asm
)

set(KERNEL_CORE_SOURCES
    kernel/core/Kernel.c
    kernel/core/Panic.c
    kernel/core/Compositor.c
    kernel/core/InitRD.c
)

set(SCHED_SOURCES
    kernel/sched/MLFQ.c
)

set(KERNEL_ETC_SOURCES
    kernel/etc/Shell.c
    kernel/etc/Console.c
    kernel/etc/Format.c
    kernel/etc/VBEConsole.c
    kernel/etc/Editor.c
    kernel/etc/StringOps.c
    kernel/etc/POST.c
)

set(ATOMIC_IPC_SOURCES
    kernel/atomic/Atomics.c
    kernel/ipc/Ipc.c
)

set(EXECF_SOURCES
    kernel/execf/elf/ELFloader.c
    kernel/execf/pe/PEloader.c
    kernel/execf/aout/AoutLoader.c
    kernel/execf/ExecLoader.c
)

set(MM_SOURCES
    mm/PMem.c
    mm/MemOps.c
    mm/KernelHeap.c
    mm/VMem.c
    mm/StackGuard.c
    mm/MemPool.c
    mm/trace/StackTrace.c
    mm/security/Cerberus.c
    mm/PageFaultHandler.c
)

set(FS_SOURCES
    fs/VFRFS.c
    fs/FAT/FAT1x.c
    fs/EXT/Ext2.c
    fs/Iso9660.c
    fs/VFS.c
    fs/BlockDevice.c
    fs/FileSystem.c
    fs/MBR.c
)

set(DRIVER_SOURCES
    drivers/APIC.c
    drivers/TSC.c
    drivers/ACPI.c
    drivers/Serial.c
    drivers/PS2.c
    drivers/Ide.c
    drivers/Vesa.c
    drivers/PCI/PCI.c
    drivers/RTC/Rtc.c
    drivers/ethernet/realtek/RTL8139.c
    drivers/ethernet/intel/E1000.c
    drivers/ethernet/interface/Ip.c
    drivers/ethernet/interface/Arp.c
    drivers/ethernet/interface/Icmp.c
    drivers/ethernet/Network.c
    drivers/xHCI/xHCI.c
    drivers/ISA/ISA.c
    drivers/sound/SB16.c
    drivers/sound/Generic.c
    drivers/storage/AHCI.c
    drivers/LPT/LPT.c
    drivers/virtio/VirtioBlk.c
    drivers/vmware/SVGAII.c
)

set(ARCH_SOURCES
    arch/x86_64/idt/Idt.c
    arch/x86_64/gdt/Gdt.c
    arch/x86_64/interrupts/Interrupts.c
    arch/x86_64/syscall/Syscall.c
    arch/x86_64/features/x64.c
)

set(INCLUDE_SOURCES
    include/ctype.c
    include/Font.c
)

set(CPP_SOURCES
    ports/6502/6502.cpp
)

set(C_SOURCES
    ${KERNEL_CORE_SOURCES}
    ${SCHED_SOURCES}
    ${KERNEL_ETC_SOURCES}
    ${ATOMIC_IPC_SOURCES}
    ${EXECF_SOURCES}
    ${MM_SOURCES}
    ${FS_SOURCES}
    ${DRIVER_SOURCES}
    ${ARCH_SOURCES}
    ${INCLUDE_SOURCES}
)

set(OBJ_SOURCES)
if(NOT EXCLUDE_EXTRA_OBJECTS)
    set(OBJ_SOURCES
        kernel/etc/objects/splash1.o
        kernel/etc/objects/panic.o
    )
endif()

# ============================================================================ 
# Build Include Directories
# ============================================================================ 
include_directories(
    .
    include
    kernel/atomic
    kernel/core
    kernel/ipc
    kernel/sched
    kernel/etc
    kernel/execf
    kernel/execf/elf
    kernel/execf/pe
    kernel/execf/aout
    drivers
    drivers/PCI
    drivers/ethernet
    drivers/ethernet/intel
    drivers/ethernet/realtek
    drivers/ethernet/interface
    drivers/RTC
    drivers/xHCI
    drivers/ISA
    drivers/sound
    drivers/storage
    drivers/virtio
    drivers/vmware
    fs
    fs/FAT
    fs/EXT
    mm
    mm/trace
    mm/security
    ports/6502
    ports
    arch/x86_64/features
    arch/x86_64/gdt
    arch/x86_64/idt
    arch/x86_64/interrupts
    arch/x86_64/syscall
)

# ============================================================================ 
# Kernel Linking
# ============================================================================ 
add_executable(voidframe.krnl
    ${C_SOURCES}
    ${CPP_SOURCES}
    ${ASM_SOURCES}
)

# Add extra object files if not excluded
if(NOT EXCLUDE_EXTRA_OBJECTS)
    target_sources(voidframe.krnl PRIVATE ${OBJ_SOURCES})
endif()

# Configure the linker to use ld.lld with proper arguments
set_target_properties(voidframe.krnl PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/voidframe.ld"
)

# Set linker flags for this specific target
target_link_options(voidframe.krnl PRIVATE 
    -fuse-ld=lld
    -T ${CMAKE_CURRENT_SOURCE_DIR}/voidframe.ld
    -nostdlib
    -static
    -Wl,-melf_x86_64
)

# ============================================================================ 
# ISO Creation
# ============================================================================ 
add_custom_command(
    OUTPUT VoidFrame.iso
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/isodir/boot/grub
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/isodir/EFI/BOOT
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/isodir/bin
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:voidframe.krnl> ${CMAKE_CURRENT_BINARY_DIR}/isodir/boot/voidframe.krnl
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/grub.cfg ${CMAKE_CURRENT_BINARY_DIR}/isodir/boot/grub/grub.cfg
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/grub.cfg ${CMAKE_CURRENT_BINARY_DIR}/isodir/EFI/BOOT/grub.cfg
    COMMAND ${GRUB_MKRESCUE} --fonts= --locales= -o ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso ${CMAKE_CURRENT_BINARY_DIR}/isodir
    DEPENDS voidframe.krnl
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building VoidFrame.iso"
    VERBATIM
)

add_custom_target(iso ALL DEPENDS VoidFrame.iso)