# The VoidFrame monolithic kernel 💫

## Table of Contents
- [Architecture](#architecture)
- [Boot Process](#boot-process)

### Key Features
- [here!](ROADMAP.md)
### System Requirements
- Hardware requirements
  - AVX2/SSE2 support
  - 64-bit support
  - PS/2 keyboard and mouse
  - 16MB of RAM (minimum)
  - 1G of RAM (recommended)
  - FAT12-formatted IDE drive (optional)
  - xHCI controller (optional)
  - RTL8139 (optional)
- Supported architecture
  - x86_64

## Architecture

### Core Components
#### Assembly Bootstrap (PXS0):
- **Purpose**: Switch to long mode and initialize the stack for the kernel
- **Location**: `arch/x86_64/asm/pxs.asm`
- **Key files**: `pxs.asm`
- **Note**: If the kernel is run with `-debugcon stdio`, string: `1KCSWF23Z456789` will be expected at stdio, any missing characters will identify a major issue.

#### C-level Bootstrap: (PXS1 & PXS2)
- **Purpose**: Initialize core kernel subsystems and jump to the higher half
- **Location**: `kernel/core`
- **Key files**: `Kernel.c`

## Boot Process

### Boot Sequence Overview
1. **Bootloader Phase**
    - GRUB loads /boot/voidframe.krnl @ 0x100000
    - Request framebuffer and VESA (800x600)

2. **Assembly bootstrap**
    - File: `arch/x86_64/asm/pxs.asm`
    - Store magic and info in eax and ebx (1)
    - Setup stack (K)
    - Check and enable features, extensions (FCS)
    - Load GDT (2)
    - Enable PAE (3)
    - Setup and zero page tables (Z)
    - Set CR3 (4)
    - Map 4G with 2MB pages (5)
    - Enable Long mode (6)
    - Enable Paging (7)
    - Entered 64-bit mode (8)
    - Setup stack & Jump to kernel entry point (9)
    - Extra: (R!) Kernel returns.

3. **PXS1**
    - Entry function: `KernelMain()` & `PXS1()`
    - Check for magic and info
    - Set fallback VGA mode buffer 0xB8000 
    - Start Serial driver across all ports
    - Start VESA driver
    - Start console driver
    - Parse MB2 info
    - Start PMM (Physical Memory Manager)
    - Create new PML4
    - Identity-map kernel sections
    - Jump to Virtual half

4. **PXS2**
    - Entry function: `KernelMainHigherHalf()` & `PXS2()`
    - Check CPU features
    - Validate memory layout
    - Start VMM
    - Start Kernel HEAP
    - Start Memory pools
    - Initialize GDT & IDT
    - Install PIC and PIT
    - Start PS/2 Drivers
    - Start Shell
    - Start Process management (and core subsystems)
    - Start IDE driver
    - Start ramfs Driver (VFRFS)
    - Start VFS driver
    - Check huge page support
    - Start & detect ISA bus device(s)
    - Enumerate PCI device(s)
    - Start xHCI controller
    - Start RTL8139 driver
    - Start LPT driver
    - Remap IRQs
    - Start stack guard 
    - Setup memory protections
    - Enable interrupts

5. **IRQ0**
    - In the VoidFrame kernel IRQ0 (interrupt number 32) is used for normal interrupts.
    - This is where the MLFQ scheduler is called.
    - The scheduler will call the process's entry point and then return to IRQ0.
    - The process will then be scheduled again.
    - This process will repeat until the process terminates.

6. **After Startup**
    - After the kernel has started and all core subsystems are ready
    - Three (default) System processes are created:
      - `PID 0`: Idle process (runs when no other process is ready)
      - `PID 1`: Astra (Security agent of the kernel)
      - `PID 2`: VoidFrame Shell
      - `PID 3`: DynamoX (Created by Astra, served as a dynamic frequency scaling agent)
    - The shell will then wait for user input and execute commands.
