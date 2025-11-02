# VoidFrame Development Guide

This document provides instructions for setting up a development environment for VoidFrame, installing dependencies, and building the project.

---

## 1. Prerequisites

To build and run VoidFrame, you will need a specific set of tools. The primary development environment is a POSIX-compliant OS (like Linux or macOS) with an x86_64 CPU.

### Required Tools:
- **Build Systems:** `cmake`.
- **Compiler Toolchain:** `clang` (C/C++ compiler), `nasm` (assembler), `cargo` (Rust compiler).
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
sudo pacman -Syu cmake ninja clang nasm qemu-full dosfstools e2fsprogs grub xorriso mtools rustup
rustup target add x86_64-unknown-none
rustup toolchain install nightly
```

</details>

<details>
<summary><b>Debian / Ubuntu</b></summary>

```bash
# Update package list and install dependencies
sudo apt update
sudo apt install -y cmake ninja-build clang nasm qemu-system-x86 dosfstools e2fsprogs grub-pc-bin xorriso mtools
curl --proto '=https' --tlsv1.3 https://sh.rustup.rs -sSf | sh -s -- -y
source ~/.cargo/env
rustup toolchain install nightly
rustup target add x86_64-unknown-none
```

</details>

<details>
<summary><b>Fedora</b></summary>

```bash
# Install dependencies
sudo dnf install -y cmake ninja-build clang nasm qemu-system-x86 dosfstools e2fsprogs grub2-tools-extra xorriso mtools
curl --proto '=https' --tlsv1.3 https://sh.rustup.rs -sSf | sh -s -- -y
source ~/.cargo/env
rustup toolchain install nightly
rustup target add x86_64-unknown-none
```

</details>

### For macOS Users

<details>
<summary><b>Using Homebrew</b></summary>

It is recommended to use [Homebrew](https://brew.sh/) to install packages on macOS.

```bash
# Install dependencies via Homebrew
brew install cmake ninja llvm nasm qemu e2fsprogs dosfstools grub

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

After installing the dependencies, you can build the kernel using CMake.

```bash
# 1. Create a build directory
mkdir -p build && cd build

# 2. Configure the project (example for Linux)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain/linux-x64.cmake
cmake ..

# 3. Features configuration
ccmake .

# 4. Build the all targets
ninja -j$(nproc)

# 4. Run the kernel in QEMU
ninja run
```
