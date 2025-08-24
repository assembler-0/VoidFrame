# [VoidFrame] - a syscall-less monolithic kernel 💫

> A fast, simple, secure 64-bit monolithic written in C and assembly. With modern capabilities.

---

- Roadmap/Features: [here!](docs/ROADMAP.md)
- Structure: [here!](docs/STRUCTURE.md)
- How it works: [here!](docs/STARTUP.md)

---

## About

VoidFrame is a 64-bit syscall-less **monolithic** kernel designed for the x86_64 architecture written in C and assembly (nasm).
This kernel was intended and targeted for people who want to learn about operating systems and want to make a piece of their own.
As the designer of this kernel, I wanted to make something that is simple, fast, secure and easy to understand.
Which obviously means that it is not a perfect kernel. And it breaks all the time.
But I have tried my hardest to bring many security features to the kernel.
If you were to come across a bug, feel free to open an issue. Fork the repo and make a pull request.
It would be amazing if you could contribute to this project!

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

