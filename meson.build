project('voidframe', 'c',
        version : '0.0.1-alpha',
        default_options : [
            'c_std=c11',
            'optimization=2',
            'warning_level=3',
            'werror=false'
        ]
)

nasm = find_program('nasm')
grub_mkrescue = find_program('grub-mkrescue')
qemu_img  = find_program('qemu-img', required : true)
mkfs_fat  = find_program('mkfs.fat', required : true)

# Source root
src_root = './'
arch_root = src_root + 'arch/x86_64'

# Light optimization flags
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
    '-fstack-protector-strong',
    '-ffast-math'
]

# Assembly flags
asm_flags = ['-f', 'elf64']

# Include directories
inc_dirs = [
    src_root + '/include',
    src_root + '/kernel/atomic',
    src_root + '/kernel/core',
    src_root + '/kernel/ipc',
    src_root + '/kernel/memory',
    src_root + '/kernel/process',
    src_root + '/kernel/etc',
    src_root + '/kernel/elf',
    src_root + '/kernel/graphics',
    src_root + '/drivers',
    src_root + '/drivers/PCI',
    src_root + '/drivers/ethernet',
    src_root + '/drivers/RTC',
    src_root + '/drivers/USB',
    src_root + '/fs',
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
    src_root + '/include/Paging.asm'
]

# C sources (organized)
c_sources = [
    src_root + '/kernel/core/Kernel.c',
    src_root + '/kernel/core/Panic.c',
    src_root + '/kernel/memory/Memory.c',
    src_root + '/kernel/memory/MemOps.c',
    src_root + '/kernel/memory/KernelHeap.c',
    src_root + '/kernel/memory/VMem.c',
    src_root + '/kernel/memory/StackGuard.c',
    src_root + '/kernel/memory/MemPool.c',
    src_root + '/kernel/process/Process.c',
    src_root + '/kernel/etc/Shell.c',
    src_root + '/kernel/etc/Console.c',
    src_root + '/kernel/etc/VBEConsole.c',
    src_root + '/kernel/etc/Editor.c',
    src_root + '/kernel/etc/StringOps.c',
    src_root + '/kernel/atomic/Atomics.c',
    src_root + '/kernel/ipc/Ipc.c',
    src_root + '/kernel/elf/ELFloader.c',
    src_root + '/fs/Fs.c',
    src_root + '/fs/FsUtils.c',
    src_root + '/fs/FAT12.c',
    src_root + '/fs/VFS.c',
    src_root + '/drivers/Pic.c',
    src_root + '/drivers/Serial.c',
    src_root + '/drivers/PS2.c',
    src_root + '/drivers/Ide.c',
    src_root + '/drivers/VesaBIOSExtension.c',
    src_root + '/drivers/PCI/PCI.c',
    src_root + '/drivers/RTC/Rtc.c',
    src_root + '/drivers/ethernet/RTL8139.c',
    src_root + '/drivers/USB/xHCI.c',
    arch_root + '/idt/Idt.c',
    arch_root + '/gdt/Gdt.c',
    arch_root + '/interrupts/Interrupts.c',
    arch_root + '/cpu/Cpu.c'
]

obj_sources = [
	src_root + '/kernel/etc/objects/splash1.o',
	src_root + '/kernel/etc/objects/splash2.o',
	src_root + '/kernel/etc/objects/splash3.o',
	src_root + '/kernel/etc/objects/splash4.o',
	src_root + '/kernel/etc/objects/splash5.o',
	src_root + '/kernel/etc/objects/splash6.o',
	src_root + '/kernel/etc/objects/splash7.o',
	src_root + '/kernel/etc/objects/splash8.o',
	src_root + '/kernel/etc/objects/splash9.o',
	src_root + '/kernel/etc/objects/splash10.o',
	src_root + '/kernel/etc/objects/panic.o',
	src_root + '/kernel/etc/objects/info.o'
]

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
                            command : [nasm, asm_flags, '-o', '@OUTPUT@', '@INPUT@']
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
                          command : ['clang', c_flags, inc_flags, '-c', '-o', '@OUTPUT@', '@INPUT@']
    )
    c_objects += c_obj
endforeach

# Link kernel
kernel = custom_target('voidframe.krnl',
                       input : asm_objects + c_objects + obj_sources,
                       output : 'voidframe.krnl',
                       command : ['ld', '-T', meson.current_source_dir() + '/linker.ld', '-melf_x86_64', '--no-warn-rwx-segments', '-o', '@OUTPUT@'] + ['@INPUT@'],
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
                        'cp voidframe.krnl isodir/boot/voidframe.krnl && ' +
                        'cp ' + meson.current_source_dir() + '/grub.cfg isodir/boot/grub/grub.cfg && ' +
                        grub_mkrescue.full_path() + ' -o VoidFrame.iso isodir'
                    ],
                    depends : kernel,
                    build_by_default : true
)

# Run target
run_target('run',
           command : ['qemu-system-x86_64',
                      '-cpu', 'max',
                      '-cdrom', 'VoidFrame.iso',
                      '-serial', 'stdio' ,
                      '-no-reboot','-no-shutdown',
                      '-m', '1G',
                      '-drive', 'file=VoidFrameDisk.img,format=raw,if=ide',
                      '-boot', 'd',
                      '-device', 'rtl8139',
                      '-device', 'e1000',
                      '-device', 'nec-usb-xhci,id=xhci',
           ]
)

run_target('img',
          command : ['qemu-img', 'create',  '-f', 'raw', 'VoidFrameDisk.img', '1440K']
)

run_target('mkfs',
          command : ['mkfs.fat', '-F',  '12', 'VoidFrameDisk.img']
)
