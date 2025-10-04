add_rules("mode.debug", "mode.release")

-- ============================================================================
-- Project Definition
-- ============================================================================
set_project("voidframe")
set_version("0.0.2-development3")
set_languages("c11", "cxx17")

-- Force clang toolchain
set_toolchains("clang", "nasm")

-- ============================================================================
-- Target: voidframe kernel
-- ============================================================================
target("voidframe")
    -- Set the kind to a binary executable
    set_kind("binary")
    -- Set the output filename for the kernel
    set_filename("voidframe.krnl")

    -- ========================================================================
    -- Toolchain Configuration
    -- ========================================================================
    -- Use nasm for assembly files
    set_toolchains("clang","nasm")

    -- ========================================================================
    -- Source Files
    -- ========================================================================
    add_files(
        -- Assembly sources
        "arch/x86_64/asm/pxs.asm",
        "arch/x86_64/idt/IdtLoad.asm",
        "arch/x86_64/gdt/GdtTssFlush.asm",
        "arch/x86_64/interrupts/Interrupts.asm",
        "arch/x86_64/syscall/SyscallEntry.asm",
        "include/Switch.asm",

        -- Kernel core sources
        "kernel/core/Kernel.c",
        "kernel/core/Panic.c",
        "kernel/core/Compositor.c",
        "kernel/core/InitRD.c",

        -- Scheduler sources
        "kernel/sched/MLFQ.c",
        "kernel/sched/EEVDF.c",

        -- Kernel utilities
        "kernel/etc/Shell.c",
        "kernel/etc/Console.c",
        "kernel/etc/Format.c",
        "kernel/etc/VBEConsole.c",
        "kernel/etc/Editor.c",
        "kernel/etc/StringOps.c",
        "kernel/etc/POST.c",

        -- Atomics and IPC
        "kernel/atomic/Atomics.c",
        "kernel/ipc/Ipc.c",

        -- Executable format loaders
        "kernel/execf/elf/ELFloader.c",
        "kernel/execf/pe/PEloader.c",
        "kernel/execf/aout/AoutLoader.c",
        "kernel/execf/ExecLoader.c",

        -- Memory management
        "mm/PMem.c",
        "mm/MemOps.c",
        "mm/KernelHeap.c",
        "mm/VMem.c",
        "mm/StackGuard.c",
        "mm/MemPool.c",
        "mm/trace/StackTrace.c",
        "mm/security/Cerberus.c",
        "mm/PageFaultHandler.c",

        -- Filesystem sources
        "fs/VFRFS.c",
        "fs/FAT/FAT1x.c",
        "fs/EXT/Ext2.c",
        "fs/Iso9660.c",
        "fs/VFS.c",
        "fs/BlockDevice.c",
        "fs/FileSystem.c",
        "fs/MBR.c",

        -- Driver sources
        "drivers/APIC.c",
        "drivers/TSC.c",
        "drivers/ACPI.c",
        "drivers/Serial.c",
        "drivers/PS2.c",
        "drivers/Ide.c",
        "drivers/Vesa.c",
        "drivers/PCI/PCI.c",
        "drivers/RTC/Rtc.c",
        "drivers/ethernet/realtek/RTL8139.c",
        "drivers/ethernet/intel/E1000.c",
        "drivers/ethernet/interface/Ip.c",
        "drivers/ethernet/interface/Arp.c",
        "drivers/ethernet/interface/Icmp.c",
        "drivers/ethernet/Network.c",
        "drivers/xHCI/xHCI.c",
        "drivers/ISA/ISA.c",
        "drivers/sound/SB16.c",
        "drivers/sound/Generic.c",
        "drivers/storage/AHCI.c",
        "drivers/LPT/LPT.c",
        "drivers/virtio/VirtioBlk.c",
        "drivers/vmware/SVGAII.c",

        -- Architecture specific sources
        "arch/x86_64/idt/Idt.c",
        "arch/x86_64/gdt/Gdt.c",
        "arch/x86_64/interrupts/Interrupts.c",
        "arch/x86_64/syscall/Syscall.c",
        "arch/x86_64/features/x64.c",

        -- Include and font sources
        "include/ctype.c",
        "include/Font.c",

        -- C++ sources
        "ports/6502/6502.cpp"
    )

    -- Add external object files
    add_links("kernel/etc/objects/splash1.o", "kernel/etc/objects/panic.o")

    -- ========================================================================
    -- Include Directories
    -- ========================================================================
    add_includedirs(
        ".",
        "include",
        "kernel/atomic",
        "kernel/core",
        "kernel/ipc",
        "kernel/sched",
        "kernel/etc",
        "kernel/execf",
        "kernel/execf/elf",
        "kernel/execf/pe",
        "kernel/execf/aout",
        "drivers",
        "drivers/PCI",
        "drivers/ethernet",
        "drivers/ethernet/intel",
        "drivers/ethernet/realtek",
        "drivers/ethernet/interface",
        "drivers/RTC",
        "drivers/xHCI",
        "drivers/ISA",
        "drivers/sound",
        "drivers/storage",
        "drivers/virtio",
        "drivers/vmware",
        "fs",
        "fs/FAT",
        "fs/EXT",
        "mm",
        "mm/trace",
        "mm/security",
        "ports/6502",
        "ports",
        "arch/x86_64/features",
        "arch/x86_64/gdt",
        "arch/x86_64/idt",
        "arch/x86_64/interrupts",
        "arch/x86_64/syscall"
    )

    -- ========================================================================
    -- Compiler and Linker Flags
    -- ========================================================================
    -- Base C flags
    add_cflags(
        "-m64",
        "-O2",
        "-fno-omit-frame-pointer",
        "-finline-functions",
        "-foptimize-sibling-calls",
        "-nostdinc",
        "-nostdlib",
        "-fno-builtin",
        "-ffreestanding",
        "-mno-red-zone",
        "-mserialize",
        "-fPIE",
        "-fPIC",
        "-mcmodel=kernel",
        "-fcf-protection=full",
        "-fvisibility=hidden",
        {force = true}
    )

    -- C++ specific flags
    add_cxflags(
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-threadsafe-statics",
        {force = true}
    )

    -- Assembly flags
    add_asflags("-f elf64", {force = true})

    -- Linker script
    set_ldscript("voidframe.ld")
    add_ldflags("-melf_x86_64", {force = true})

    -- ========================================================================
    -- VoidFrame Configuration Defines
    -- ========================================================================
    add_defines(
        "VF_CONFIG_ENABLE_XHCI",
        "VF_CONFIG_ENABLE_VIRTIO",
        "VF_CONFIG_ENABLE_ISA",
        "VF_CONFIG_ENABLE_LPT",
        "VF_CONFIG_ENABLE_PCI",
        "VF_CONFIG_ENABLE_PS2",
        "VF_CONFIG_ENABLE_IDE",
        "VF_CONFIG_ENABLE_VFCOMPOSITOR",
        "VF_CONFIG_ENABLE_AHCI",
        "VF_CONFIG_ENABLE_GENERIC_SOUND",
        "VF_CONFIG_RTC_CENTURY",
        "VF_CONFIG_LOAD_MB_MODULES",
        "VF_CONFIG_ENFORCE_MEMORY_PROTECTION",
        "VF_CONFIG_VM_HOST",
        "VF_CONFIG_SNOOZE_ON_BOOT",
        "VF_CONFIG_PROCINFO_CREATE_DEFAULT",
        "VF_CONFIG_USE_VFSHELL",
        "VF_CONFIG_USE_DYNAMOX",
        "VF_CONFIG_USE_ASTRA",
        "VF_CONFIG_USE_CERBERUS",
        "VF_CONFIG_CERBERUS_STACK_PROTECTION",
        "VF_CONFIG_SCHED_MLFQ"
--         "VF_CONFIG_SCHED_EEVDF"
    )

    add_asdefines(
        "VF_CONFIG_VESA_FB"
    )

-- ============================================================================
-- Custom Tasks
-- ============================================================================

task("iso")
    set_category("build")
    set_description("Create bootable ISO image")
    on_run(function ()
        os.run("mkdir -p build/isodir/boot/grub")
        os.cp("build/linux/x86_64/release/voidframe.krnl", "build/isodir/boot/voidframe.krnl")
        os.cp("grub.cfg", "build/isodir/boot/grub/grub.cfg")
        os.run("grub-mkrescue -o build/VoidFrame.iso build/isodir")
    end)

task("run")
    set_category("run")
    set_description("Run VoidFrame in QEMU")
    after_run(function ()
        os.run([[ 
            qemu-system-x86_64 \
                -cpu max \
                -vga vmware \
                -enable-kvm \
                -cdrom build/VoidFrame.iso \
                -debugcon file:bootstrap.log \
                -serial stdio \
                -no-reboot -no-shutdown \
                -m 4G \
                -drive file=VoidFrameDisk.img,if=ide \
                -drive file=SataDisk.img,if=none,id=sata0 \
                -device ahci,id=ahci \
                -device ide-hd,drive=sata0,bus=ahci.0 \
                -boot d \
                -device rtl8139 \
                -device e1000 \
                -device nec-usb-xhci,id=xhci \
                -device ich9-intel-hda \
                -usb -device usb-tablet \
                -audiodev pa,id=myaudio \
                -device sb16,iobase=0x220,irq=5,dma=1,dma16=5 \
                -parallel file:printer.out \
                -drive file=VirtioDisk.img,format=raw,id=virtio_disk,if=none \
                -device virtio-blk-pci,drive=virtio_disk,disable-legacy=on
        ]])
    end)

task("runmin")
    set_category("run")
    set_description("Run VoidFrame in QEMU (minimal)")
    after_run(function ()
        os.run([[ 
            qemu-system-x86_64 \
                -cdrom build/VoidFrame.iso \
                -nographic \
                -debugcon file:bootstrap.log \
                -serial file:serial.log \
                -no-reboot -no-shutdown \
                -m 4G \
                -boot d
        ]])
    end)

task("img")
    set_category("run")
    set_description("Create disk images")
    on_run(function ()
        os.run("qemu-img create -f qcow2 VoidFrameDisk.img 16G")
        os.run("mkfs.ext2 VoidFrameDisk.img")
    end)

task("extra-img")
    set_category("run")
    set_description("Create extra disk images")
    on_run(function ()
        os.run("qemu-img create -f raw VirtioDisk.img 128M")
        os.run("mkfs.fat -F 16 VirtioDisk.img")
        os.run("qemu-img create -f raw SataDisk.img 128M")
        os.run("mkfs.fat -F 16 SataDisk.img")
    end)

task("dump")
    set_category("run")
    set_description("Generate disassembly and symbols")
    on_run(function ()
        os.run("llvm-objdump -d build/linux/x86_64/release/voidframe.krnl > voidframe.dump")
        os.run("llvm-objdump -t build/linux/x86_64/release/voidframe.krnl > voidframe.sym")
    end)
