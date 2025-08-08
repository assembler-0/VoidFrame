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
- [ ] Per-process page tables
- [ ] User-mode memory protection

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
- [ ] CFS or vruntime-based scheduler (optional/bonus)

---

## 🔐 Ring 3 Support (Userspace) (uhhh)

- [ ] User-mode process flag
- [ ] IRETQ from syscall/interrupt
- [ ] Syscall handling (`syscall` or `int 0x80`)
- [ ] Userland stack setup
- [ ] Memory isolation (Ring 3 can't touch kernel)
- [ ] Transitioning back to kernel on syscall

---

## 🧩 Module System

- [x] Multiboot2 module loading
- [ ] Basic kernel extensions
- [ ] Signature/token validation
- [ ] Hot module unloading (optional)
- [ ] Module registration system

---

## 📦 ELF Executable Support

- [ ] ELF64 header parsing
- [ ] Program header mapping (`PT_LOAD`)
- [ ] Set up new stack
- [ ] Jump to entry point
- [ ] Static binaries only (no relocations)
- [ ] Dynamic linker / interpreter (WAY later)

---

## 🧠 Init System

- [ ] Create `init` process from `vmod` or ELF
- [x] Init spawns shell or TUI

---

## 💬 IPC / Syscalls

- [x] Syscall dispatch system
- [x] Basic message passing (pipe, queue, or buffer)
- [ ] Shared memory region
- [ ] Signals or async delivery
- [ ] Named channels or sockets

---

## 📁 Filesystem & I/O

- [ ] Initrd loading
- [ ] Basic filesystem parsing (e.g., tarfs or ext2-lite)
- [ ] Read/write file API (`fs_open()`, etc.)
- [ ] VFS layer (optional)
- [ ] Device files (`/dev/null`, `/dev/tty0`, etc.)

---

## 🧑‍💻 Userland Development

- [ ] Userspace C runtime (libc-lite)
- [ ] Shell (`sh.vmod` or `sh.elf`)
- [ ] Basic CLI utilities (`cat`, `ls`, `echo`, etc.)
- [ ] Keyboard driver routed to userland
- [ ] VGA console or terminal emulator

---

## 🔧 Debug & Developer Features

- [x] Print kernel logs to VGA
- [x] Serial logging
- [x] `dmesg`-style kernel log buffer
- [ ] Stack backtrace / panic debug dump
- [ ] Memory usage counters

---

## 🌈 Extra Spice (optional but cool)

- [ ] Framebuffer graphics support (UEFI mode)
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
- [ ] At least one userland demo program
