# Building the VoidFrame microkernel

---

## Prerequisites

- meson >= 1.0.0
- ninja >= 1.11
- clang >= 18.0.0 (or any C-compliant compiler)
- nasm >= 2.16
- qemu >= 7.0.0
- mkfs.fat (dosfstools)
- grub-mkrescue
  - Note: depending on your distro, grub-mkrescue may require xorriso and mtools packages.

### Quickstart
```bash
git clone https://github.com/assembler-0/VoidFrame.git
cd VoidFrame
meson setup build
cd build
ninja
ninja img && ninja mkfs # Optional
ninja run
```

