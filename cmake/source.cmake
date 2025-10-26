# ============================================================================
# Sources Organization
# ============================================================================
set(ASM_SOURCES
        arch/x86_64/asm/pxs.asm
        arch/x86_64/idt/IdtLoad.asm
        arch/x86_64/gdt/GdtTssFlush.asm
        arch/x86_64/interrupts/Interrupts.asm
        arch/x86_64/syscall/SyscallEntry.asm
        include/Switch.asm
        mm/asm/memcpy.asm
        mm/asm/memset.asm
        mm/asm/memcmp.asm
        mm/asm/zeropage.asm
)

set(KERNEL_CORE_SOURCES
        kernel/core/Kernel.c
        kernel/core/Panic.c
        kernel/core/InitRD.c
)

set(VFC_SOURCES
        vfcompositor/Compositor.c
        vfcompositor/app/GUIShell.c
)

set(SCHED_SOURCES
        kernel/sched/MLFQ.c
        kernel/sched/EEVDF.c
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
        kernel/execf/macho/MachoLoader.c
        kernel/execf/ExecLoader.c
)

set(MM_SOURCES
        mm/PMem.c
        mm/MemOps.c
        mm/VMem.c
        mm/StackGuard.c
        mm/MemPool.c
        mm/trace/StackTrace.c
        mm/security/Cerberus.c
        mm/dynamic/c/Magazine.c
        mm/PageFaultHandler.c
)

set(FS_SOURCES
        fs/VFRFS.c
        fs/FAT/FAT1x.c
        fs/EXT/Ext2.c
        fs/NTFS/NTFS.c
        fs/Iso9660.c
        fs/VFS.c
        fs/BlockDevice.c
        fs/FileSystem.c
        fs/MBR.c
        fs/DriveNaming.c
)

set(DRIVER_SOURCES
        drivers/APIC/APIC.c
        drivers/OPIC/OPIC.c
        drivers/TSC.c
        drivers/ACPI.c
        drivers/Serial.c
        drivers/PS2.c
        drivers/storage/Ide.c
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
        drivers/usb/hid/USBKeyboard.c
        drivers/ISA/ISA.c
        drivers/sound/SB16.c
        drivers/sound/Generic.c
        drivers/storage/AHCI.c
        drivers/storage/NVMe.c
        drivers/LPT/LPT.c
        drivers/virtio/VirtioBlk.c
        drivers/vmware/SVGAII.c
        drivers/input/Keyboard.c
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
        include/Io.c
)

set(CRYPTO_SOURCES
        crypto/RNG.c
        crypto/CRC32.c
        crypto/SHA256.c
)

set(CPP_SOURCES
        ports/6502/6502.cpp
)

set(OBJ_SOURCES)

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
        drivers/APIC
        drivers/OPIC
        drivers/usb
        drivers/usb/hid
        drivers/input
        fs
        fs/FAT
        fs/EXT
        fs/NTFS
        mm
        mm/dynamic
        mm/dynamic/c
        mm/dynamic/rust
        mm/trace
        mm/security
        ports/6502
        ports
        ports/raytracer
        arch/x86_64/features
        arch/x86_64/gdt
        arch/x86_64/idt
        arch/x86_64/interrupts
        arch/x86_64/syscall
        vfcompositor
        vfcompositor/app
        crypto
)

# ============================================================================
# Sources Configuration
# ============================================================================

if(NOT EXCLUDE_EXTRA_OBJECTS)
    set(OBJ_SOURCES
            kernel/etc/objects/splash1.o
            kernel/etc/objects/panic.o
    )
endif()

# ============================================================================
# Final Source List
# ============================================================================
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
        ${VFC_SOURCES}
        ${CRYPTO_SOURCES}
)