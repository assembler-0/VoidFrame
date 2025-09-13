# [VoidFrame] - a syscall-less monolithic kernel 💫

> A fast, simple, secure 64-bit monolithic written in C and assembly. With modern capabilities.

---

- Roadmap/Features: [here!](docs/ROADMAP.md)
- Structure: [here!](docs/STRUCTURE.md)
- How it works: [here!](docs/ARCHITECTURE.md)

---

## Status

![License](https://img.shields.io/badge/License-GPLv2-orange)

![Version](https://img.shields.io/badge/Current%20Version-v0.0.2%20development2-blue)

![Build](https://img.shields.io/badge/Build-passing-brightgreen)

## About

VoidFrame is a 64-bit syscall-less **monolithic** kernel (sounds weird and contradicting right?) designed for the x86_64 architecture written in C and assembly (nasm).
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
- qemu >= 7.0.0 (minimum, failed to run on Limbo emulator (v5.x))
- mkfs.fat (dosfstools)
- mkfs.ext2
- grub-mkrescue
    - Note: depending on your distro, grub-mkrescue may require xorriso and mtools packages.

### Quickstart
#### Full development setup
```bash
git clone https://github.com/assembler-0/VoidFrame.git
cd VoidFrame
python scripts/vfconfig.py
meson setup build
cd build
ninja
ninja img
ninja extra-img
ninja run
```
#### Minimal setup
```bash
git clone https://github.com/assembler-0/VoidFrame.git
cd VoidFrame
meson setup build -Dexclude_extra_objects=true
cd build
ninja
ninja runmin
```

## Features
### Architecture
- [x] x86_64
- [ ] AArch64
- [ ] MIPS
- [ ] SPARC
- [ ] RISC-V (RV64)
- [ ] Power (modern)
### Boot
- [x] Multiboot2
- [x] GRUB (BIOS)
- [ ] GRUB (UEFI)
- [ ] GRUB (Hybrid)
- [x] Vesa (VBE)
- [x] Multiboot2 Info parsing
### Core
- [x] Multi-tasking (MLFQ)
- [x] Per-process authentication check (Astra)
- [x] Dynamic ML-inspired PIT frequency scaling (DynamoX)
- [x] Virtual Memory (canonical)
- [x] Physical Memory
- [x] Memory Pool
- [x] AVX2/SSE2 accelerated memory operations
- [x] Memory & user protection
- [x] Memory canaries, guard pages
- [x] Per-process memory checks (Cerberus)
- [x] Stack guard
- [x] Stack trace
- [x] Heap (Class-based)
- [x] Paging
- [x] Interrupts
- [x] Process Management
- [x] Locks (MCS/RW/norm)
- [x] Atomics
- [x] IPC
- [x] Compositor
- [x] Embedded shell
- [x] Builtin Editor
- [x] ELF64 loader
- [x] PE32+/COFF loader
- [x] a.out loader
### Filesystems
- FAT1x
    - [x] Read
    - [x] Write
    - [x] Create
    - [x] Delete
    - [x] List
- EXT2
    - [x] Read
    - [x] Write
    - [x] Create
    - [x] Delete
    - [x] List
- VFRFS (VoidFrame RAMFS)
    - [x] Read
    - [x] Write
    - [x] Create
    - [x] Delete
    - [x] List
- ISO9660 (RO)
    - [x] Read
    - [x] List
- VFS (Virtual File System)
    - [x] Full abstraction
    - [x] EXT2
    - [x] FAT1x
    - [x] VFRFS
    - [ ] ISO9660 
### Drivers
- Network
    - [x] RTL8139 (PCI)
    - [x] E1000   (PCI)
- Network Interface
    - [x] IP
    - [x] ARP
    - [x] ICMP
- Sound
    - [x] SB16 (PCI)
    - [x] Generic PC speaker
- USB
    - [x] xHCI
- VirtIO
    - [x] Block
- Graphics
    - [x] Vesa (VBE)
    - [x] VMWare SVGA II
    - [x] VGA text mode
- Timer
    - [x] PIT
    - [x] PIC
    - [x] APIC (not working)
    - [x] RTC
- Generic
    - [x] PCI
    - [x] ISA
    - [x] xHCI
    - [x] Serial
    - [x] PS/2
    - [x] LPT
- Storage
    - [x] PATA (IDE)
    - [x] VirtIO Block
    - [x] AHCI