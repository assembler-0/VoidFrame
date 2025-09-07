project('voidframe', 'c',
        version : '0.0.2-rc1',
        default_options : [
            'c_std=c11',
            'optimization=2',
            'warning_level=3',
            'werror=false'
        ]
)

clang = find_program('clang', required : true)
ld = find_program('ld', required : true)
nasm = find_program('nasm', required : true)
grub_mkrescue = find_program('grub-mkrescue')
qemu_img  = find_program('qemu-img')
mkfs_fat  = find_program('mkfs.fat')
mkfs_ext2 = find_program('mkfs.ext2')
qemu_system_x86_64 = find_program('qemu-system-x86_64')

exclude_extra_objects = get_option('exclude_extra_objects')

# Source root
src_root = './'
arch_root = src_root + 'arch/x86_64'

# C flags
c_flags = [
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
    '-fno-pie',
    '-fno-pic',
    '-mcmodel=kernel',
    '-fcf-protection=full',
    '-fstack-protector-strong',
    '-D_FORTIFY_SOURCE=2',
    '-fvisibility=hidden',
    '-Wall',
    '-Wpedantic',
    '-Wextra',
]

# Assembly flags
asm_flags = ['-f', 'elf64']

# Include directories
inc_dirs = [
    src_root + '/include',
    src_root + '/kernel/atomic',
    src_root + '/kernel/core',
    src_root + '/kernel/ipc',
    src_root + '/kernel/sched',
    src_root + '/kernel/etc',
    src_root + '/kernel/elf',
    src_root + '/kernel/graphics',
    src_root + '/drivers',
    src_root + '/drivers/PCI',
    src_root + '/drivers/ethernet',
    src_root + '/drivers/RTC',
    src_root + '/drivers/xHCI',
    src_root + '/drivers/ISA',
    src_root + '/drivers/sound',
    src_root + '/drivers/virtio',
    src_root + '/drivers/vmware',
    src_root + '/fs',
    src_root + '/fs/FAT',
    src_root + '/fs/EXT',
    src_root + '/mm',
    src_root + '/mm/trace',
    src_root + '/mm/security',
    arch_root + '/cpu',
    arch_root + '/gdt',
    arch_root + '/idt',
    arch_root + '/interrupts'
]

# Assembly sources (organized)
asm_sources = [
    arch_root + '/asm/pxs.asm',
    arch_root + '/idt/IdtLoad.asm',
    arch_root + '/gdt/GdtTssFlush.asm',
    arch_root + '/interrupts/Interrupts.asm',
    src_root + '/include/Switch.asm'
]

# C sources (organized)
c_sources = [
    src_root + '/kernel/core/Kernel.c',
    src_root + '/kernel/core/Panic.c',
    src_root + '/kernel/core/Compositor.c',
    src_root + '/kernel/sched/MLFQ.c',
    src_root + '/kernel/etc/Shell.c',
    src_root + '/kernel/etc/Console.c',
    src_root + '/kernel/etc/Format.c',
    src_root + '/kernel/etc/VBEConsole.c',
    src_root + '/kernel/etc/Editor.c',
    src_root + '/kernel/etc/StringOps.c',
    src_root + '/kernel/atomic/Atomics.c',
    src_root + '/kernel/ipc/Ipc.c',
    src_root + '/kernel/elf/ELFloader.c',
    src_root + '/mm/PMem.c',
    src_root + '/mm/MemOps.c',
    src_root + '/mm/KernelHeap.c',
    src_root + '/mm/VMem.c',
    src_root + '/mm/StackGuard.c',
    src_root + '/mm/MemPool.c',
    src_root + '/mm/trace/StackTrace.c',
    src_root + '/mm/security/Cerberus.c',
    src_root + '/fs/VFRFS.c',
    src_root + '/fs/FAT/FAT1x.c',
    src_root + '/fs/EXT/Ext2.c',
    src_root + '/fs/Iso9660.c',
    src_root + '/fs/VFS.c',
    src_root + '/drivers/Pic.c',
    src_root + '/drivers/Serial.c',
    src_root + '/drivers/PS2.c',
    src_root + '/drivers/Ide.c',
    src_root + '/drivers/Vesa.c',
    src_root + '/drivers/PCI/PCI.c',
    src_root + '/drivers/RTC/Rtc.c',
    src_root + '/drivers/ethernet/RTL8139.c',
    src_root + '/drivers/xHCI/xHCI.c',
    src_root + '/drivers/ISA/ISA.c',
    src_root + '/drivers/sound/SB16.c',
    src_root + '/drivers/LPT/LPT.c',
    src_root + '/drivers/APIC.c',
    src_root + '/drivers/InterruptController.c',
    src_root + '/drivers/virtio/VirtioBlk.c',
    src_root + '/drivers/vmware/SVGAII.c',
    src_root + '/include/ctype.c',
    src_root + '/include/Font.c',
    arch_root + '/idt/Idt.c',
    arch_root + '/gdt/Gdt.c',
    arch_root + '/interrupts/Interrupts.c',
    arch_root + '/cpu/Cpu.c'
]

obj_sources = [
    src_root + '/kernel/etc/objects/splash1.o',
    src_root + '/kernel/etc/objects/panic.o',
]

vf_config_flags =  [
    '-DVF_CONFIG_ENABLE_XHCI',
    '-DVF_CONFIG_ENABLE_VIRTIO',
    '-DVF_CONFIG_ENABLE_ISA',
    '-DVF_CONFIG_ENABLE_LPT',
    '-DVF_CONFIG_ENABLE_PCI',
    '-DVF_CONFIG_ENABLE_PS2',
    '-DVF_CONFIG_ENABLE_IDE',
    '-DVF_CONFIG_ENABLE_VFCOMPOSITOR',
#    '-DVF_CONFIG_ENABLE_VMWARE_SVGA_II',
    '-DVF_CONFIG_RTC_CENTURY',
    '-DVF_CONFIG_ENFORCE_MEMORY_PROTECTION',
    '-DVF_CONFIG_VM_HOST',
    '-DVF_CONFIG_SNOOZE_ON_BOOT',
    '-DVF_CONFIG_PROCINFO_CREATE_DEFAULT',
    '-DVF_CONFIG_USE_VFSHELL',
    '-DVF_CONFIG_USE_DYNAMOX',
    '-DVF_CONFIG_USE_ASTRA',
    '-DVF_CONFIG_USE_CERBERUS',
    '-DVF_CONFIG_CERBERUS_VFS_LOGGING',
    '-DVF_CONFIG_CERBERUS_THREAT_REPORTING',
    #'-DVF_CONFIG_CERBERUS_STACK_PROTECTION',
    '-DVF_CONFIG_SCHED_MLFQ',
    '-DVF_CONFIG_PROCINFO_AUTO_CLEANUP',
    #'-DVF_CONFIG_PANIC_OVERRIDE',
]

if exclude_extra_objects
    vf_config_flags += ['-DVF_CONFIG_EXCLUDE_EXTRA_OBJECTS']
endif

cfg_flags = []
foreach flag : vf_config_flags
    cfg_flags += flag
endforeach

# Build include flags
inc_flags = []
foreach inc : inc_dirs
    inc_flags += ['-I' + meson.current_source_dir() + '/' + inc]
endforeach

# Compile assembly files
asm_objects = []
foreach asm_file : asm_sources
    obj_name = asm_file.split('/')[-1].split('.')[0] + '_asm.o'
    asm_obj = custom_target('asm_' + obj_name,
                            input : asm_file,
                            output : obj_name,
                            command : [nasm.full_path(), asm_flags, '-o', '@OUTPUT@', '@INPUT@']
    )
    asm_objects += asm_obj
endforeach

# Compile C files
c_objects = []
foreach c_file : c_sources
    obj_name = c_file.split('/')[-1].split('.')[0] + '_c.o'
    c_obj = custom_target('c_' + obj_name,
                          input : c_file,
                          output : obj_name,
                          command : [clang.full_path(), c_flags, inc_flags, cfg_flags, '-c', '-o', '@OUTPUT@', '@INPUT@']
    )
    c_objects += c_obj
endforeach

obj_to_link = []
if exclude_extra_objects
    obj_to_link = asm_objects + c_objects
else
    obj_to_link = asm_objects + c_objects + obj_sources
endif

# Link kernel
kernel = custom_target('voidframe.krnl',
                       input : obj_to_link,
                       output : 'voidframe.krnl',
                       command : [ld.full_path(), '-T', meson.current_source_dir() + '/linker.ld', '-melf_x86_64', '--no-warn-rwx-segments', '-o', '@OUTPUT@'] + ['@INPUT@'],
                       build_by_default : true
)

# Create ISO
iso = custom_target('VoidFrame.iso',
                    input : [kernel, 'grub.cfg'],
                    output : 'VoidFrame.iso',
                    command : [
                        'sh', '-c',
                        'cd ' + meson.current_build_dir() + ' && ' +
                        'mkdir -p isodir/boot/grub && ' +
                        'mkdir -p isodir/EFI/BOOT && ' +
                        'mkdir -p isodir/bin && ' +
                        'cp voidframe.krnl isodir/boot/voidframe.krnl && ' +
                        'cp ' + meson.current_source_dir() + '/grub.cfg isodir/boot/grub/grub.cfg && ' +
                        'cp ' + meson.current_source_dir() + '/grub.cfg isodir/EFI/BOOT/grub.cfg && ' +
                        grub_mkrescue.full_path() + ' --modules="multiboot2 part_msdos part_gpt fat" --fonts="" --locales=""' + ' -o VoidFrame.iso isodir'
                    ],
                    depends : kernel,
                    build_by_default : true
)

# Run target
run_target('run',
           command : [qemu_system_x86_64.full_path(),
                      '-cpu', 'max',
                      '-enable-kvm',
                      '-cdrom', 'VoidFrame.iso',
                      '-debugcon', 'file:bootstrap.log',
                      '-serial', 'stdio',
                      '-no-reboot','-no-shutdown',
                      '-m', '4G',
                      '-hda', 'VoidFrameDisk.img',
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
           ]
)

run_target('runmin',
           command : [qemu_system_x86_64.full_path(),
                      '-cdrom', 'VoidFrame.iso',
                      '-nographic',
                      '-debugcon', 'file:bootstrap.log',
                      '-serial', 'file:serial.log',
                      '-no-reboot','-no-shutdown',
                      '-m', '4G',
                      '-boot', 'd',
           ]
)


run_target('img',
               command : [
                 'sh', '-c',
                 qemu_img.full_path() + ' create -f qcow2 VoidFrameDisk.img 16G' + ' && ' +
                 mkfs_ext2.full_path() + ' VoidFrameDisk.img'
               ]
)

run_target('virtio-img',
    command: ['sh', '-c',
              qemu_img.full_path() + ' create -f raw VirtioDisk.img 128M' + ' && ' +
              mkfs_fat.full_path() + ' -F 16 VirtioDisk.img'
    ]
)
