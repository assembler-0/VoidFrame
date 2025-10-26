# [VoidFrame] - a ring 0 kernel 💫

> A fast, simple, secure 64-bit ring-0 kernel written in C and assembly. With modern capabilities.

---

- Roadmap/Features: [here!](docs/ROADMAP.md)
- How it works: [here!](docs/ARCHITECTURE.md)
- Development Guide: [here!](docs/DEVELOPMENT.md)

---

## Status
![CI/CD](https://github.com/assembler-0/VoidFrame/actions/workflows/main.yaml/badge.svg)
![License](https://img.shields.io/badge/License-GPLv2-blue)
![Version](https://img.shields.io/badge/Current%20Version-v0.0.2%20development4-brightgreen)

## About

VoidFrame is a 64-bit **ring-0** kernel designed for the x86_64 architecture written in C and assembly (nasm).
This kernel was intended and targeted for people who want to learn about operating systems and want to make a piece of their own.
As the designer of this kernel, I just wanted to make something that is simple, fast, secure and easy to understand.
Which obviously means that it is not a perfect kernel. And it breaks all the time.
But I have tried my hardest to bring many security features to the kernel.
If you were to come across a bug, feel free to open an issue. Fork the repo and make a pull request.
It would be amazing if you could contribute to this project!

## Prerequisites (development)
- x64-compatible cpu (used: Intel i3-12100F)
- POSIX-compliant OS (SysV ABI) (used: Arch Linux 6.16.9-arch1-1)
- cmake >= 3.20 (used: cmake 4.1.2)
- meson >= 1.4 (used: meson 1.9.1)
- ninja >= 1.11 (used: ninja 1.21.1)
- clang/++ >= 18.0.0 (used: 20.1.8)
- rustup (nightly, bare metal toolchain) >= 1.89.0 (used: 1.92.0-nightly)
- nasm >= 2.16 (used: 2.16.03)
- qemu >= 7.0.0 (used: 10.1.0)
- mkfs.fat
- mkfs.ext2
- grub-mkrescue (used: 2:2.12.r359.g19c698d12-1)
    - Note: depending on your distro, grub-mkrescue may require xorriso and mtools packages.

### Quickstart
#### Full development setup
```bash
# Meson
git clone https://github.com/assembler-0/VoidFrame.git
cd VoidFrame
meson setup build
cd build
ninja -j$(nproc)
ninja img
ninja extra-img
ninja run
```
```bash
# CMake
git clone https://github.com/assembler-0/VoidFrame.git
cd VoidFrame
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain/<linux/windows/macos>-x64.cmake \
    -G Ninja \
    -DVF_SCHEDULER=<MLFQ/EEVDF> \
    -DVF_CONFIG_HEAP_LANG=<C/RUST>
ccmake . # Optinal, tune as needed
ninja -j$(nproc)
ninja run
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
- [x] MLFQ
- [x] EEVDF
- [ ] SMP
- [ ] Threads
- [x] Per-process authentication check (Astra)
- [x] Dynamic ML-inspired PIT frequency scaling (DynamoX)
- [x] Virtual Memory (canonical)
- [x] Physical Memory
- [x] Dynamic memory mapping
- [x] Memory Pool
- [x] AVX/2/512 SSE/SSE2 accelerated memory operations
- [x] Memory & user protection
- [x] Memory canaries, guard pages
- [x] Per-process memory checks (Cerberus)
- [x] Stack guard
- [x] Stack trace
- [x] Heap (C & Rust modules)
- [x] Paging
- [x] Interrupts
- [x] Process Management
- [x] Locks (MCS/RW/norm in C and Rust)
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
- NTFS
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
- Timers
    - [x] PIT (for APIC calibration)
    - [x] PIC (remnants)
    - [x] APIC (with timer)
    - [x] LAPIC
    - [x] IOAPIC
    - [x] RTC
    - [x] TSC (usable delay(), finally!)
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
    - [x] NVMe