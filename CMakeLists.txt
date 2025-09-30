cmake_minimum_required(VERSION 3.20)
project(voidframe VERSION 0.0.2 LANGUAGES C CXX ASM_NASM)
enable_language(ASM_NASM)
# ============================================================================ 
# Tool Dependencies
# ============================================================================ 
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_LINKER ld.lld)
set(CMAKE_ASM_NASM_COMPILER nasm)

find_program(LLVM_OBJDUMP llvm-objdump)
find_program(GRUB_MKRESCUE grub-mkrescue)
find_program(QEMU_IMG qemu-img)
find_program(MKFS_FAT mkfs.fat)
find_program(MKFS_EXT2 mkfs.ext2)
find_program(QEMU_SYSTEM_X86_64 qemu-system-x86_64)

# ============================================================================ 
# Build Configuration
# ============================================================================ 
option(EXCLUDE_EXTRA_OBJECTS "Exclude extra objects from the build" OFF)
option(AUTOMATIC_POST "Run POST automatically on boot" OFF)
option(DEBUG_SYMBOLS "Enable debug symbols" ON)
option(STACK_PROTECTION "Enable stack protection" ON)

# ============================================================================ 
# Compiler Flags
# ============================================================================ 
set(C_FLAGS " -m64 -O2 -fno-omit-frame-pointer -finline-functions -foptimize-sibling-calls -nostdinc -nostdlib -fno-builtin -ffreestanding -mno-red-zone -mserialize -fPIE -fPIC -mcmodel=kernel -fcf-protection=full -fvisibility=hidden")

if(SILENT_BUILD)
    string(APPEND C_FLAGS " -w")
endif()

if(STACK_PROTECTION)
    string(APPEND C_FLAGS " -fstack-protector-strong -D_FORTIFY_SOURCE=2")
endif()

if(DEBUG_SYMBOLS)
    string(APPEND C_FLAGS " -g -DDEBUG")
endif()

set(CMAKE_C_FLAGS "${C_FLAGS}")
set(CMAKE_CXX_FLAGS "${C_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics")
set(CMAKE_ASM_NASM_FLAGS "-f elf64 -DVF_CONFIG_VESA_FB")

# ============================================================================ 
# VoidFrame Configuration Flags
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

if(EXCLUDE_EXTRA_OBJECTS)
    add_compile_definitions(VF_CONFIG_EXCLUDE_EXTRA_OBJECTS)
endif()

if(AUTOMATIC_POST)
    add_compile_definitions(VF_CONFIG_AUTOMATIC_POST)
endif()

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
    DEPENDS iso
)

add_custom_target(runmin
    COMMAND ${QEMU_SYSTEM_X86_64}
        -cdrom ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso
        -nographic
        -debugcon file:bootstrap.log
        -serial file:serial.log
        -no-reboot -no-shutdown
        -m 4G
        -boot d
    DEPENDS iso
)

add_custom_target(img
    COMMAND ${QEMU_IMG} create -f qcow2 VoidFrameDisk.img 16G && ${MKFS_EXT2} VoidFrameDisk.img
    COMMENT "Creating disk images"
)

add_custom_target(extra-img
    COMMAND ${QEMU_IMG} create -f raw VirtioDisk.img 128M && ${MKFS_FAT} -F 16 VirtioDisk.img && ${QEMU_IMG} create -f raw SataDisk.img 128M && ${MKFS_FAT} -F 16 SataDisk.img
    COMMENT "Creating extra disk images"
)

add_custom_target(dump
    COMMAND sh -c "${LLVM_OBJDUMP} -d $<TARGET_FILE:voidframe.krnl> > voidframe.dump && ${LLVM_OBJDUMP} -t $<TARGET_FILE:voidframe.krnl> > voidframe.sym"
    DEPENDS voidframe.krnl
    COMMENT "Generating disassembly and symbols"
)
