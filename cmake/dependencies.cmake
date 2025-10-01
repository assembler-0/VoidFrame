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