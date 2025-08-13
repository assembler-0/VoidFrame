# Building the VoidFrame microkernel

---

### Prerequisites
```bash
meson => 1.0.0
clang => 18.0.0 (or any C-compliant compiler)
nasm => 2.16
qemu => 7.0.0
mkfs.fat utility
grub-mkrescue utility
```

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

