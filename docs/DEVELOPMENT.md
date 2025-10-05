# VoidFrame Development Guide

This document provides instructions for setting up a development environment for VoidFrame, installing dependencies, and building the project.

---

## 1. Prerequisites

To build and run VoidFrame, you will need a specific set of tools. The primary development environment is a POSIX-compliant OS (like Linux or macOS) with an x86_64 CPU.

### Required Tools:
- **Build Systems:** At least one of `cmake`, `meson`.
- **Compiler Toolchain:** `clang` (C/C++ compiler) and `nasm` (assembler).
- **Build Executor:** `ninja`.
- **Emulator:** `qemu`.
- **Filesystem Tools:** `mkfs.fat`, `mkfs.ext2`.
- **Bootloader Tool:** `grub-mkrescue` (which may also require `xorriso` and `mtools`).

---

## 2. Dependency Installation

Here are the commands to install the required dependencies on various platforms.

### For Linux Users

<details>
<summary><b>Arch Linux</b></summary>

```bash
# Update package list and install dependencies
sudo pacman -Syu cmake meson ninja clang nasm qemu-full dosfstools e2fsprogs grub xorriso mtools
```

</details>

<details>
<summary><b>Debian / Ubuntu</b></summary>

```bash
# Update package list and install dependencies
sudo apt update
sudo apt install -y cmake meson ninja-build clang nasm qemu-system-x86 dosfstools e2fsprogs grub-pc-bin xorriso mtools
```

</details>

<details>
<summary><b>Fedora</b></summary>

```bash
# Install dependencies
sudo dnf install -y cmake meson ninja-build clang nasm qemu-system-x86 dosfstools e2fsprogs grub2-tools-extra xorriso mtools
```

</details>

### For macOS Users

<details>
<summary><b>Using Homebrew</b></summary>

It is recommended to use [Homebrew](https://brew.sh/) to install packages on macOS.

```bash
# Install dependencies via Homebrew
brew install cmake meson ninja llvm nasm qemu e2fsprogs dosfstools grub

# Add LLVM to your PATH so the build system can find clang
echo 'export PATH="/usr/local/opt/llvm/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

</details>

### For Windows Users

<details>
<summary><b>Using Windows Subsystem for Linux (WSL)</b></summary>

Native development on Windows is not recommended due to the difficulty in setting up the required kernel development tools (like GRUB and `mkfs`).

The recommended approach is to use [WSL](https://learn.microsoft.com/en-us/windows/wsl/install), which allows you to run a Linux environment directly on Windows.

1.  Install WSL with a distribution like **Ubuntu**. 
2.  Open your WSL terminal.
3.  Follow the **Debian / Ubuntu** instructions above to install the dependencies.

</details>

---

## 3. Building VoidFrame

After installing the dependencies, you can build the kernel using one of the supported build systems. All commands should be run from the root of the VoidFrame repository.

### Using CMake (actively maintained)

```bash
# 1. Create a build directory
mkdir -p build && cd build

# 2. Configure the project (example for Linux)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain/linux-x64.cmake

# 3. Build the kernel
ninja

# 4. Run the kernel in QEMU
ninja run
```

### Using Meson (maintained)

```bash
# 1. Set up the build directory
meson setup build

# 2. Navigate to the build directory
cd build

# 3. Build the kernel
ninja

# 4. Run the kernel in QEMU
ninja run
```