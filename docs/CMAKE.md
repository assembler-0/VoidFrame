# VoidFrame Build System Documentation

This document provides an overview of the CMake-based build system for the VoidFrame kernel.

## 1. Overview

The build system is designed to be modular and easy to understand. It uses CMake to manage the compilation and linking of the kernel, as well as to create the final bootable ISO image. The main configuration is split across several files located in the `/cmake` directory, which are included from the root `CMakeLists.txt`.

## 2. Main `CMakeLists.txt`

This is the main entry point for the build system. It performs the following tasks:

-   **Project Setup:** Defines the project name (`VoidFrame`), version, and languages used (C, CXX, ASM_NASM).
-   **Platform Checks:** Ensures the build is running on a supported host system (Linux). It will exit with an error on Windows and show a warning on macOS.
-   **Standard Configuration:** Sets the C and CXX language standards to C11 and C++20, respectively.
-   **Module Includes:** Includes all the modular CMake scripts from the `/cmake` directory.
-   **Source File Organization:** Groups all the source files (`.c`, `.cpp`, `.asm`) into different categories (e.g., `KERNEL_CORE_SOURCES`, `DRIVER_SOURCES`). This makes it easy to see which files belong to which part of the kernel.
-   **Include Directories:** Specifies all the directories that the compiler needs to search for header files.
-   **Kernel Linking:** Defines the `voidframe.krnl` executable, which is the compiled kernel. It links all the source files together and uses the `voidframe.ld` linker script.
-   **ISO Creation:** Creates a custom command to generate the `VoidFrame.iso` bootable image using `grub-mkrescue`. This command also sets up the directory structure for the ISO, copies the kernel and the `grub.cfg` file.

## 3. CMake Modules (`/cmake`)

The `/cmake` directory contains several `.cmake` files that handle specific parts of the build configuration.

### `clang-x64.cmake`

This file configures the build to use the Clang toolchain for cross-compilation to an x86_64 target.
-   Sets the C and CXX compilers to `clang` and `clang++`.
-   Sets the assembler to `nasm`.
-   Configures the linker to use `lld`.

### `configuration.cmake`

This file defines several build options that can be configured when running CMake.
-   `EXCLUDE_EXTRA_OBJECTS`: Excludes extra object files (like splash screens) from the build.
-   `AUTOMATIC_POST`: Runs the Power-On Self Test (POST) automatically on boot.
-   `DEBUG_SYMBOLS`: Enables debug symbols in the build.
-   `STACK_PROTECTION`: Enables stack protection.
-   `SILENT_BUILD`: Suppresses compiler warnings.

### `dependencies.cmake`

This script checks for the presence of all the required tools for building and running the kernel, such as `llvm-objdump`, `grub-mkrescue`, `qemu-img`, `mkfs.fat`, `mkfs.ext2`, and `qemu-system-x86_64`. It will print a warning if any of these are not found.

### `features.cmake`

This file defines a set of preprocessor macros that enable or disable specific kernel features. This allows for a highly configurable kernel build. Some of the features include:
-   Drivers for different hardware (XHCI, VirtIO, PCI, etc.).
-   Kernel features like the compositor, memory protection, and different schedulers.
-   Debugging and logging features.

### `flags.cmake`

This file sets the compiler and assembler flags for the build.
-   **C/C++ Flags:** Includes flags for optimization (`-O2`), target architecture (`-m64`), and disables standard library includes (`-nostdinc`, `-nostdlib`). It also includes security features like Control-Flow Protection (`-fcf-protection=full`) and stack protection.
-   **Debug Flags:** Adds `-g3` for debug symbols if `DEBUG_SYMBOLS` is enabled.
-   **NASM Flags:** Sets the output format to `elf64`.

### `run.cmake`

This file defines several custom targets for running and testing the kernel.
-   `run`: The main target to run the kernel in QEMU with a comprehensive set of virtual hardware.
-   `runmin`: A minimal QEMU target with no graphics, for quick tests.
-   `img`: A target to create a 16GB qcow2 disk image.
-   `extra-img`: Creates additional disk images for VirtIO and SATA testing.
-   `dump`: Generates a disassembly (`.dump`) and symbol table (`.sym`) of the kernel.

### `variable.cmake`

This file defines a set of preprocessor macros that configure various kernel constants, such as:
-   `MAX_PROCESSES`
-   `PAGE_SIZE`
-   `KERNEL_STACK_SIZE`
-   Memory layout addresses (e.g., `KERNEL_VIRTUAL_OFFSET`).

## 4. How to Build

To build the kernel, you can use the standard CMake workflow:

1.  **Configure:** `cmake -B build`
2.  **Build:** `cmake --build build`

This will generate the `voidframe.krnl` file and the `VoidFrame.iso` in the `build` directory.

To run the kernel in QEMU, you can use the `run` target:

`cmake --build build --target run`