project('voidframe', 'c', 'cpp',
        version : '0.0.2-development3',
        meson_version : '>= 1.4.0',
        default_options : [
            'c_std=c11',
            'cpp_std=c++17',
            'optimization=2',
            'warning_level=3',
            'werror=false'
        ]
)

# ============================================================================
# Tool Dependencies
# ============================================================================
clang = find_program('clang', required : true)
clangpp = find_program('clang++', required : true)
lld = find_program('ld.lld', required : true)
nasm = find_program('nasm', required : true)
llvm_objdump = find_program('llvm-objdump')
grub_mkrescue = find_program('grub-mkrescue')
qemu_img = find_program('qemu-img')
mkfs_fat = find_program('mkfs.fat')
mkfs_ext2 = find_program('mkfs.ext2')
qemu_system_x86_64 = find_program('qemu-system-x86_64')

# ============================================================================
# Build Configuration
# ============================================================================
exclude_extra_objects = get_option('exclude_extra_objects')
automatic_post = get_option('automatic_post')
enable_debug = get_option('debug_symbols')
enable_stack_protection = get_option('stack_protection')

# ============================================================================
# Directory Structure
# ============================================================================
src_root = meson.current_source_dir()
arch_root = src_root / 'arch' / 'x86_64'

# ============================================================================
# Build Options
# ============================================================================
silent_build = get_option('silent_build')

# ============================================================================
# Compiler Flags
# ============================================================================
base_c_flags = [
    '-m64',
    '-O2',
    '-fno-omit-frame-pointer',
    '-finline-functions',
    '-foptimize-sibling-calls',
    '-nostdinc',
    '-nostdlib',
    '-fno-builtin',
    '-ffreestanding',
    '-mno-red-zone',
    '-mserialize',
    '-fPIE',
    '-fPIC',
    '-mcmodel=kernel',
    '-fcf-protection=full',
    '-fvisibility=hidden',
]

if silent_build
    base_c_flags += ['-w']
endif

# Add conditional flags
c_flags = base_c_flags
if enable_stack_protection
    c_flags += ['-fstack-protector-strong', '-D_FORTIFY_SOURCE=2']
endif

if enable_debug
    c_flags += ['-g', '-DDEBUG']
endif

cpp_flags = c_flags + [
    '-fno-exceptions',
    '-fno-rtti',
    '-fno-threadsafe-statics',
]

asm_flags = ['-f', 'elf64']

# ============================================================================
# VoidFrame Configuration Flags
# ============================================================================
vf_config_base = [
    '-DVF_CONFIG_ENABLE_XHCI',
    '-DVF_CONFIG_ENABLE_VIRTIO',
    '-DVF_CONFIG_ENABLE_ISA',
    '-DVF_CONFIG_ENABLE_LPT',
    '-DVF_CONFIG_ENABLE_PCI',
    '-DVF_CONFIG_ENABLE_PS2',
    '-DVF_CONFIG_ENABLE_IDE',
    '-DVF_CONFIG_ENABLE_VFCOMPOSITOR',
    '-DVF_CONFIG_ENABLE_AHCI',
    '-DVF_CONFIG_ENABLE_GENERIC_SOUND',
    '-DVF_CONFIG_RTC_CENTURY',
    '-DVF_CONFIG_LOAD_MB_MODULES',
    '-DVF_CONFIG_ENFORCE_MEMORY_PROTECTION',
    '-DVF_CONFIG_VM_HOST',
    '-DVF_CONFIG_SNOOZE_ON_BOOT',
    '-DVF_CONFIG_PROCINFO_CREATE_DEFAULT',
    '-DVF_CONFIG_USE_VFSHELL',
    '-DVF_CONFIG_USE_DYNAMOX',
    '-DVF_CONFIG_USE_ASTRA',
    '-DVF_CONFIG_USE_CERBERUS',
    '-DVF_CONFIG_CERBERUS_STACK_PROTECTION',
    '-DVF_CONFIG_SCHED_MLFQ',
]

vf_config_optional = [
    # '-DVF_CONFIG_ENABLE_VMWARE_SVGA_II',
    # '-DVF_CONFIG_CERBERUS_VFS_LOGGING',
    # '-DVF_CONFIG_CERBERUS_THREAT_REPORTING',
    # '-DVF_CONFIG_PROCINFO_AUTO_CLEANUP',
    # '-DVF_CONFIG_PANIC_OVERRIDE',
]

vf_config_asm = [
    '-DVF_CONFIG_VESA_FB',
]

asm_flags += vf_config_asm

vf_config_flags = vf_config_base
if exclude_extra_objects
    vf_config_flags += ['-DVF_CONFIG_EXCLUDE_EXTRA_OBJECTS']
endif
if automatic_post
    vf_config_flags += ['-DVF_CONFIG_AUTOMATIC_POST']
endif

# ============================================================================
# Source Files Organization
# ============================================================================

# Assembly sources
asm_sources = files([
                        'arch/x86_64/asm/pxs.asm',
                        'arch/x86_64/idt/IdtLoad.asm',
                        'arch/x86_64/gdt/GdtTssFlush.asm',
                        'arch/x86_64/interrupts/Interrupts.asm',
                        'arch/x86_64/syscall/SyscallEntry.asm',
                        'include/Switch.asm'
                    ])

# Kernel core sources
kernel_core_sources = files([
                                'kernel/core/Kernel.c',
                                'kernel/core/Panic.c',
                                'kernel/core/Compositor.c',
                                'kernel/core/InitRD.c',
                            ])

# Scheduler sources
sched_sources = files([
                          'kernel/sched/MLFQ.c',
                      ])

# Kernel utilities
kernel_etc_sources = files([
                               'kernel/etc/Shell.c',
                               'kernel/etc/Console.c',
                               'kernel/etc/Format.c',
                               'kernel/etc/VBEConsole.c',
                               'kernel/etc/Editor.c',
                               'kernel/etc/StringOps.c',
                               'kernel/etc/POST.c',
                           ])

# Atomics and IPC
atomic_ipc_sources = files([
                               'kernel/atomic/Atomics.c',
                               'kernel/ipc/Ipc.c',
                           ])

# Executable format loaders
execf_sources = files([
                          'kernel/execf/elf/ELFloader.c',
                          'kernel/execf/pe/PEloader.c',
                          'kernel/execf/aout/AoutLoader.c',
                          'kernel/execf/ExecLoader.c',
                      ])

# Memory management
mm_sources = files([
                       'mm/PMem.c',
                       'mm/MemOps.c',
                       'mm/KernelHeap.c',
                       'mm/VMem.c',
                       'mm/StackGuard.c',
                       'mm/MemPool.c',
                       'mm/trace/StackTrace.c',
                       'mm/security/Cerberus.c',
                       'mm/PageFaultHandler.c',
                   ])

# Filesystem sources
fs_sources = files([
                       'fs/VFRFS.c',
                       'fs/FAT/FAT1x.c',
                       'fs/EXT/Ext2.c',
                       'fs/Iso9660.c',
                       'fs/VFS.c',
                       'fs/BlockDevice.c',
                       'fs/FileSystem.c',
                       'fs/MBR.c',
                   ])

# Driver sources
driver_sources = files([
                           'drivers/APIC.c',
                           'drivers/TSC.c',
                           'drivers/ACPI.c',
                           'drivers/Serial.c',
                           'drivers/PS2.c',
                           'drivers/Ide.c',
                           'drivers/Vesa.c',
                           'drivers/PCI/PCI.c',
                           'drivers/RTC/Rtc.c',
                           'drivers/ethernet/realtek/RTL8139.c',
                           'drivers/ethernet/intel/E1000.c',
                           'drivers/ethernet/interface/Ip.c',
                           'drivers/ethernet/interface/Arp.c',
                           'drivers/ethernet/interface/Icmp.c',
                           'drivers/ethernet/Network.c',
                           'drivers/xHCI/xHCI.c',
                           'drivers/ISA/ISA.c',
                           'drivers/sound/SB16.c',
                           'drivers/sound/Generic.c',
                           'drivers/storage/AHCI.c',
                           'drivers/LPT/LPT.c',
                           'drivers/virtio/VirtioBlk.c',
                           'drivers/vmware/SVGAII.c',
                       ])

# Architecture specific sources
arch_sources = files([
                         'arch/x86_64/idt/Idt.c',
                         'arch/x86_64/gdt/Gdt.c',
                         'arch/x86_64/interrupts/Interrupts.c',
                         'arch/x86_64/syscall/Syscall.c',
                         'arch/x86_64/features/x64.c'
                     ])

# Include and font sources
include_sources = files([
                            'include/ctype.c',
                            'include/Font.c',
                        ])

# C++ sources
cpp_sources = files([
                        'ports/6502/6502.cpp',
                    ])

# Combine all C sources
c_sources = (kernel_core_sources + sched_sources + kernel_etc_sources +
    atomic_ipc_sources + execf_sources + mm_sources + fs_sources +
    driver_sources + arch_sources + include_sources)

# Extra object files
obj_sources = files([
                        'kernel/etc/objects/splash1.o',
                        'kernel/etc/objects/panic.o',
                    ])

# ============================================================================
# Build Functions
# ============================================================================

# Build Include Directories
inc_dirs_list = [
    '.',
    'include',
    'kernel/atomic',
    'kernel/core',
    'kernel/ipc',
    'kernel/sched',
    'kernel/etc',
    'kernel/execf',
    'kernel/execf/elf',
    'kernel/execf/pe',
    'kernel/execf/aout',
    'drivers',
    'drivers/PCI',
    'drivers/ethernet',
    'drivers/ethernet/intel',
    'drivers/ethernet/realtek',
    'drivers/ethernet/interface',
    'drivers/RTC',
    'drivers/xHCI',
    'drivers/ISA',
    'drivers/sound',
    'drivers/storage',
    'drivers/virtio',
    'drivers/vmware',
    'fs',
    'fs/FAT',
    'fs/EXT',
    'mm',
    'mm/trace',
    'mm/security',
    'ports/6502',
    'ports',
    'arch/x86_64/features',
    'arch/x86_64/gdt',
    'arch/x86_64/idt',
    'arch/x86_64/interrupts',
    'arch/x86_64/syscall',
]

inc_flags = []
foreach inc : inc_dirs_list
    inc_flags += ['-I' + src_root + '/' + inc]
endforeach

# Function to compile assembly files
asm_objects = []
foreach asm_file : asm_sources
    obj_name = '@0@_asm.o'.format(asm_file.full_path().split('/')[-1].split('.')[0])
    asm_obj = custom_target(
        obj_name,
        input : asm_file,
        output : obj_name,
        command : [nasm, asm_flags, '-o', '@OUTPUT@', '@INPUT@'],
        build_by_default : false
    )
    asm_objects += asm_obj
endforeach

# Function to compile C files
c_objects = []
foreach c_file : c_sources
    obj_name = '@0@_c.o'.format(c_file.full_path().split('/')[-1].split('.')[0])
    c_obj = custom_target(
        obj_name,
        input : c_file,
        output : obj_name,
        command : [clang, c_flags, inc_flags, vf_config_flags, '-c', '-o', '@OUTPUT@', '@INPUT@'],
        build_by_default : false
    )
    c_objects += c_obj
endforeach

# Function to compile C++ files
cpp_objects = []
foreach cpp_file : cpp_sources
    obj_name = '@0@_cpp.o'.format(cpp_file.full_path().split('/')[-1].split('.')[0])
    cpp_obj = custom_target(
        obj_name,
        input : cpp_file,
        output : obj_name,
        command : [clangpp, cpp_flags, inc_flags, vf_config_flags, '-c', '-o', '@OUTPUT@', '@INPUT@'],
        build_by_default : false
    )
    cpp_objects += cpp_obj
endforeach

# ============================================================================
# Kernel Linking
# ============================================================================
obj_to_link = asm_objects + c_objects + cpp_objects
if not exclude_extra_objects
    obj_to_link += obj_sources
endif

kernel = custom_target(
    'voidframe.krnl',
    input : obj_to_link,
    output : 'voidframe.krnl',
    command : [
        lld,
        '-T', src_root / 'voidframe.ld',
        '-melf_x86_64',
        '-o', '@OUTPUT@',
        '@INPUT@'
    ],
    build_by_default : true,
    install : false
)

# ============================================================================
# ISO Creation
# ============================================================================
iso = custom_target(
    'VoidFrame.iso',
    input : [kernel, files('grub.cfg')],
    output : 'VoidFrame.iso',
    command : [
        'sh', '-c', '''
        cd @0@ && \
        mkdir -p isodir/boot/grub isodir/EFI/BOOT isodir/bin && \
        cp @1@ isodir/boot/voidframe.krnl && \
        cp @2@ isodir/boot/grub/grub.cfg && \
        cp @2@ isodir/EFI/BOOT/grub.cfg && \
        @3@ --modules="multiboot2 part_msdos part_gpt fat" --fonts="" --locales="" -o @4@ isodir
        '''.format(
            meson.current_build_dir(),
            kernel.full_path(),
            meson.current_source_dir() / 'grub.cfg',
            grub_mkrescue.full_path(),
            '@OUTPUT@'
        )
    ],
    depends : kernel,
    build_by_default : true
)

# ============================================================================
# Run Targets
# ============================================================================

# Full QEMU run with all devices
run_target('run',
           command : [
               qemu_system_x86_64,
               '-cpu', 'max',
               '-vga', 'vmware',
               '-enable-kvm',
               '-cdrom', iso.full_path(),
               '-debugcon', 'file:bootstrap.log',
               '-serial', 'stdio',
               '-no-reboot', '-no-shutdown',
               '-m', '4G',
               '-drive', 'file=VoidFrameDisk.img,if=ide',
               '-drive', 'file=SataDisk.img,if=none,id=sata0',
               '-device', 'ahci,id=ahci',
               '-device', 'ide-hd,drive=sata0,bus=ahci.0',
               '-boot', 'd',
               '-device', 'rtl8139',
               '-device', 'e1000',
               '-device', 'nec-usb-xhci,id=xhci',
               '-device', 'ich9-intel-hda',
               '-usb', '-device', 'usb-tablet',
               '-audio', 'pa,id=myaudio',
               '-device', 'sb16,iobase=0x220,irq=5,dma=1,dma16=5',
               '-parallel', 'file:printer.out',
               '-drive', 'file=VirtioDisk.img,format=raw,id=virtio_disk,if=none',
               '-device', 'virtio-blk-pci,drive=virtio_disk,disable-legacy=on',
           ],
           depends : iso
)

# Minimal QEMU run
run_target('runmin',
           command : [
               qemu_system_x86_64,
               '-cdrom', iso.full_path(),
               '-nographic',
               '-debugcon', 'file:bootstrap.log',
               '-serial', 'file:serial.log',
               '-no-reboot', '-no-shutdown',
               '-m', '4G',
               '-boot', 'd',
           ],
           depends : iso
)

# Create disk images
run_target('img',
           command : [
               'sh', '-c',
               '@0@ create -f qcow2 VoidFrameDisk.img 16G && @1@ VoidFrameDisk.img'.format(
                   qemu_img.full_path(),
                   mkfs_ext2.full_path()
               )
           ]
)

run_target('extra-img',
           command : [
               'sh', '-c',
               '''@0@ create -f raw VirtioDisk.img 128M && \
           @1@ -F 16 VirtioDisk.img && \
           @0@ create -f raw SataDisk.img 128M && \
           @1@ -F 16 SataDisk.img'''.format(
                   qemu_img.full_path(),
                   mkfs_fat.full_path()
               )
           ]
)

# Generate disassembly and symbols
run_target('dump',
           command : [
               'sh', '-c',
               '''@0@ -d @1@ > voidframe.dump && \
           @0@ -t @1@ > voidframe.sym'''.format(
                   llvm_objdump.full_path(),
                   kernel.full_path()
               )
           ],
           depends : kernel
)
