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

# Light optimization flags
c_flags = [
  '-m64',
  '-O2',
  '-fno-omit-frame-pointer',
  '-finline-functions',

  '-foptimize-sibling-calls',
  '-nostdinc',
  '-fno-builtin',
  '-ffreestanding',
  '-fno-stack-protector',
  '-ffast-math'
]

# Assembly flags
asm_flags = ['-f', 'elf64', '-O2']

# Include directories
inc_dirs = [
  'Kernel/Core',
  'Kernel/Memory', 
  'Kernel/Process',
  'Kernel/System',
  'Kernel/Drivers'
]

# Assembly sources (organized)
asm_sources = [
  'Boot/boot.asm',
  'Kernel/System/IdtLoad.asm',
  'Kernel/System/GdtFlush.asm',
  'Kernel/System/SyscallEntry.asm',
  'Kernel/Process/Switch.asm',
  'Kernel/Drivers/Interrupts.asm'
]

# C sources (organized)
c_sources = [
  'Kernel/Core/Kernel.c',
  'Kernel/Core/Panic.c',
  'Kernel/Memory/Memory.c',
  'Kernel/Memory/MemOps.c',
  'Kernel/Memory/VMem.c',
  'Kernel/Process/Process.c',
  'Kernel/Process/UserMode.c',
  'Kernel/System/Idt.c',
  'Kernel/System/Gdt.c',
  'Kernel/System/Syscall.c',
  'Kernel/Drivers/Interrupts.c',
  'Kernel/Drivers/Pic.c',
  'Kernel/Drivers/Cpu.c',
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
  input : asm_objects + c_objects,
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
  command : ['qemu-system-x86_64', '-cdrom', 'VoidFrame.iso', '-no-reboot', '-no-shutdown' , '-m', '1G']
)