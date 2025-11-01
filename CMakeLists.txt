# ============================================================================
# VoidFrame CMake Build Script - v0.0.2-development4
# ============================================================================
cmake_minimum_required(VERSION 3.30)
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)
project(VoidFrame
        VERSION 0.0.2
        LANGUAGES C CXX ASM_NASM
        HOMEPAGE_URL "https://github.com/assembler-0/VoidFrame"
        DESCRIPTION "A hobbyist operating system kernel written in C and Rust"
)
enable_language(ASM_NASM)

# ============================================================================
# Module Path & Includes
# ============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(cache)
include(ccache)
include(features)
include(variable)
include(dependencies)
include(configuration)
include(source)
include(rust_utils)
include(flags)

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

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(WARNING "CMAKE_TOOLCHAIN_FILE is not set. Automatically selecting toolchain file...")
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/linux-x64.cmake" CACHE PATH "Toolchain file" FORCE)
        message(STATUS "Defaulting to Linux x86_64 toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/macos-x64.cmake" CACHE PATH "Toolchain file" FORCE)
        message(STATUS "Defaulting to macOS x86_64 toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/windows-x64.cmake" CACHE PATH "Toolchain file" FORCE)
        message(STATUS "Defaulting to Windows x86_64 toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
    endif()
else()
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    message(WARNING "CMAKE_BUILD_TYPE not set. Defaulting to Release.")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(STATUS "CMake Target Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    set(Rust_CARGO_TARGET "x86_64-unknown-none")
else()
    message(FATAL_ERROR "Unsupported target architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "CMake Generator: ${CMAKE_GENERATOR}")
message(STATUS "CMake Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMake Source Directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMake Binary Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "CMake Current Source Directory: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "CMake Current Binary Directory: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "CMake Host System Name: ${CMAKE_HOST_SYSTEM_NAME}")
message(STATUS "CMake Host System Processor: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "VoidFrame Scheduler: ${VF_SCHEDULER}")
message(STATUS "CMake sources configured.")

# ============================================================================
# Standard Configuration
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Manifest paths for Rust components (corrosion with CMake)
# ============================================================================
set(RUST_HEAP_MANIFEST_PATH "${CMAKE_SOURCE_DIR}/mm/dynamic/rust/Cargo.toml")
set(RUST_ATOMIC_MANIFEST_PATH "${CMAKE_SOURCE_DIR}/kernel/atomic/rust/Cargo.toml")

# ============================================================================
# Corrosion
# ============================================================================

add_subdirectory(corrosion)
corrosion_import_crate(
        MANIFEST_PATH ${RUST_HEAP_MANIFEST_PATH}
        NO_STD
        ${Corrosion_CARGO_BUILD_FLAGS}
)
corrosion_import_crate(
        MANIFEST_PATH ${RUST_ATOMIC_MANIFEST_PATH}
        NO_STD
        ${Corrosion_CARGO_BUILD_FLAGS}
)

if (SILENT_BUILD)
    corrosion_add_target_rustflags(voidframe_mm "-A" "warnings" "-C" "link-arg=-s")
    corrosion_add_target_rustflags(voidframe_spinlock "-A" "warnings" "-C" "link-arg=-s")
endif()

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

# Rust libraries
link_rust_library(voidframe.krnl voidframe-spinlock)
link_rust_library(voidframe.krnl voidframe-mm)

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

# ============================================================================
# Run Targets
# ============================================================================
add_custom_target(run
        COMMAND ${QEMU_SYSTEM_X86_64}
        -cpu max
        -vga vmware
        -enable-kvm
        -cdrom ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso
        -debugcon file:bootstrap.log
        -serial stdio
        -no-reboot -no-shutdown
        -m 4G
        -drive file=VoidFrameDisk.img,if=ide
        -drive file=SataDisk.img,if=none,id=sata0
        -device ahci,id=ahci
        -device ide-hd,drive=sata0,bus=ahci.0
        -boot d
        -device rtl8139
        -device e1000
        -device nec-usb-xhci,id=xhci
        -device ich9-intel-hda
        -usb -device usb-tablet
        -audio pa,id=myaudio
        -device sb16,iobase=0x220,irq=5,dma=1,dma16=5
        -parallel file:printer.out
        -drive file=VirtioDisk.img,format=raw,id=virtio_disk,if=none
        -device virtio-blk-pci,drive=virtio_disk,disable-legacy=on
        -drive file=NVMeDisk.img,format=raw,id=nvme_disk,if=none
        -device nvme,drive=nvme_disk,serial=nvme001
        DEPENDS iso img extra-img
)

add_custom_target(runmin
        COMMAND ${QEMU_SYSTEM_X86_64}
        -cdrom ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso
        -nographic
        -debugcon file:bootstrap.log
        -serial file:serial.log
        -no-reboot -no-shutdown
        -m 1G
        -boot d
        DEPENDS iso
)

add_custom_target(img
        COMMAND ${QEMU_IMG} create -f qcow2 VoidFrameDisk.img 16G
        COMMAND ${MKFS_EXT2} VoidFrameDisk.img
        COMMENT "Creating disk images"
)

add_custom_target(extra-img
        COMMAND ${QEMU_IMG} create -f raw VirtioDisk.img 128M
        COMMAND ${MKFS_EXT2} VirtioDisk.img
        COMMAND ${QEMU_IMG} create -f raw SataDisk.img 128M
        COMMAND ${MKFS_EXT2} SataDisk.img
        COMMAND ${QEMU_IMG} create -f raw NVMeDisk.img 256M
        COMMAND ${MKFS_EXT2} NVMeDisk.img
        COMMENT "Creating extra disk images"
)

add_custom_target(dump
        COMMAND ${LLVM_OBJDUMP} -d $<TARGET_FILE:voidframe.krnl> > voidframe.dump
        COMMAND ${LLVM_OBJDUMP} -t $<TARGET_FILE:voidframe.krnl> > voidframe.sym
        DEPENDS voidframe.krnl
        COMMENT "Generating disassembly and symbols"
)
