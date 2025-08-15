# VoidFrame Kernel Development Checklist

> *This file tracks the current status of all core OS features for VoidFrame.*

---

## 🧱 Core Infrastructure

- [x] Bootloader (GRUB2 with Multiboot2)
- [x] GDT & TSS setup
- [x] IDT & Interrupt Handling
- [x] PIC remapping & IRQ handling
- [x] PIT / Timer setup
- [x] Basic `printf` to VGA text mode
- [x] Kernel memory allocator
- [x] Panic screen with ASCII art (yes this is mandatory)

---

## 🧠 Memory Management

- [x] Physical memory manager (free list or bitmap)
- [x] Page table setup (paging enabled)
- [x] Virtual memory mapping API (`vmem_map()`, etc.)
- [x] Kernel heap (virtual, malloc/free)

---

## ⚙️ Process Management & Scheduling

- [x] Process control block (PCB)
- [x] Scheduler (MLFQ)
- [x] Context switching
- [x] Preemption via PIT
- [x] Process creation
- [x] Process termination 
- [x] Token/privilege-based validation
- [x] Scheduler aging/starvation fix
---

## 🧩 Module System

- [ ] Multiboot2 module loading
- [ ] Basic kernel extensions
- [ ] Signature/token validation
- [ ] Hot module unloading (optional)
- [ ] Module registration system

---

## 📦 ELF Executable Support

- [x] ELF64 header parsing
- [x] Program header mapping (`PT_LOAD`)
- [x] Set up new stack
- [ ] Jump to entry point
- [x] Static binaries only (no relocations)
- [ ] Dynamic linker / interpreter (WAY later)

---

## 🧠 Init System

- [ ] Create `init` process from `vmod` or ELF
- [x] Init spawns shell or TUI

---

## 💬 IPC 

- [x] Basic message passing (pipe, queue, or buffer)
- [ ] Shared memory region
- [ ] Signals or async delivery
- [ ] Named channels or sockets

---

## 📁 Filesystem & I/O

- [ ] Initrd loading
- [ ] Basic filesystem parsing (e.g., tarfs or ext2-lite)
- [x] Read/write file API (`fs_open()`, etc.)
- [x] VFS layer (optional)
- [ ] Device files (`/dev/null`, `/dev/tty0`, etc.)

---

## 🧑‍💻 Userland Development

- [ ] Userspace C runtime (libc-lite)
- [x] Shell (`sh.vmod` or `sh.elf`)
- [x] Basic CLI utilities (`cat`, `ls`, `echo`, etc.)
- [x] Keyboard driver
- [x] VGA console or terminal emulator

---

## 🔧 Debug & Developer Features

- [x] Print kernel logs to VGA
- [x] Serial logging
- [x] `dmesg`-style kernel log buffer
- [x] Stack backtrace / panic debug dump
- [x] Memory usage counters

---

## 🌈 Extra Spice (optional but cool)

- [x] Framebuffer graphics support
- [ ] Loadable GUI modules
- [ ] Virtual terminal switching (`tty0`, `tty1`)
- [ ] Profiling support (ticks per process)
- [ ] Syscall tracing / log
- [ ] Live module patching

---

## 🏁 Final Goals (v1.0 release)

- [ ] Bootable ISO with GRUB2 EFI support
- [ ] Fully self-hosted userland shell
- [ ] User process execution (ELF64)
- [ ] Init system + FS + IPC working
- [ ] At least one demo program
