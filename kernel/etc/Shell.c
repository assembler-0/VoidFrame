#include "Shell.h"
#include "../elf/ELFloader.h"
#include "Console.h"
#include "Editor.h"
#include "ISA.h"
#include "KernelHeap.h"
#include "LPT/LPT.h"
#include "MemOps.h"
#include "Memory.h"
#include "PCI/PCI.h"
#include "PS2.h"
#include "Packet.h"
#include "Panic.h"
#include "Pic.h"
#include "Process.h"
#include "RTC/Rtc.h"
#include "RTL8139.h"
#include "SB16.h"
#include "Serial.h"
#include "StringOps.h"
#include "VFS.h"
#include "VMem.h"
#include "VesaBIOSExtension.h"
#include "stdlib.h"
#include "xHCI/xHCI.h"

static char command_buffer[256];
static int cmd_pos = 0;
static char current_dir[256] = "/";

static void Version() {
    PrintKernelSuccess("VoidFrame v0.0.1-beta\n");
    PrintKernelSuccess("VoidFrame Shell v0.0.1-beta\n");
}

extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t _bss_start[];
extern uint8_t _bss_end[];


void PrintKernelMemoryLayout(void) {
    PrintKernel("MEMORY LAYOUT\n");
    PrintKernel("\n=== VoidFrame Kernel Memory Map ===\n\n");

    // Physical Layout
    PrintKernel("📍 PHYSICAL MEMORY LAYOUT:\n");
    PrintKernel("  0x00000000-0x000FFFFF : Low Memory (1MB)\n");
    PrintKernel("  0x00100000-");
    PrintKernelHex((uint64_t)_kernel_phys_end);
    PrintKernel(" : Kernel Image (");
    PrintKernelInt(((uint64_t)_kernel_phys_end - (uint64_t)_kernel_phys_start) / 1024);
    PrintKernel("KB)\n");

    // Show kernel sections
    PrintKernel("    ├─ .text    : ");
    PrintKernelHex((uint64_t)_text_start);
    PrintKernel(" - ");
    PrintKernelHex((uint64_t)_text_end);
    PrintKernel(" (");
    PrintKernelInt(((uint64_t)_text_end - (uint64_t)_text_start) / 1024);
    PrintKernel("KB)\n");

    PrintKernel("    ├─ .rodata  : ");
    PrintKernelHex((uint64_t)_rodata_start);
    PrintKernel(" - ");
    PrintKernelHex((uint64_t)_rodata_end);
    PrintKernel(" (");
    PrintKernelInt(((uint64_t)_rodata_end - (uint64_t)_rodata_start) / 1024);
    PrintKernel("KB)\n");

    PrintKernel("    ├─ .data    : ");
    PrintKernelHex((uint64_t)_data_start);
    PrintKernel(" - ");
    PrintKernelHex((uint64_t)_data_end);
    PrintKernel(" (");
    PrintKernelInt(((uint64_t)_data_end - (uint64_t)_data_start) / 1024);
    PrintKernel("KB)\n");

    PrintKernel("    └─ .bss     : ");
    PrintKernelHex((uint64_t)_bss_start);
    PrintKernel(" - ");
    PrintKernelHex((uint64_t)_bss_end);
    PrintKernel(" (");
    PrintKernelInt(((uint64_t)_bss_end - (uint64_t)_bss_start) / 1024);
    PrintKernel("KB)\n");

    // Physical memory stats
    MemoryStats stats;
    GetDetailedMemoryStats(&stats);
    PrintKernel("  ");
    PrintKernelHex((uint64_t)_kernel_phys_end);
    PrintKernel("-0x???????? : Available RAM (");
    PrintKernelInt(stats.total_physical_bytes / (1024*1024));
    PrintKernel("MB total, ");
    PrintKernelInt(stats.free_physical_bytes / (1024*1024));
    PrintKernel("MB free)\n\n");

    // Virtual Layout
    PrintKernel("🗺️  VIRTUAL MEMORY LAYOUT:\n");
    PrintKernel("  0x0000000000000000-0x0000007FFFFFFFFF : User Space (128TB)\n");
    PrintKernel("  0xFFFF800000000000-0xFFFFFFFF00000000 : Heap Space (512GB)\n");
    PrintKernel("  0xFFFFFFFF80000000-0xFFFFFFFFFFFFFFFF : Kernel Space (2GB)\n");
    PrintKernel("    └─ Current kernel at: ");
    PrintKernelHex(KERNEL_VIRTUAL_BASE);
    PrintKernel("\n\n");

    // Current Memory Usage
    PrintKernel("💾 CURRENT MEMORY USAGE:\n");
    PrintKernel("  Physical Pages: ");
    PrintKernelInt(stats.used_physical_bytes / 1024 / 1024);
    PrintKernel("MB used / ");
    PrintKernelInt(stats.total_physical_bytes / 1024 / 1024);
    PrintKernel("MB total\n");
    PrintKernel("  Allocations: ");
    PrintKernelInt(stats.allocation_count);
    PrintKernel(" allocs, ");
    PrintKernelInt(stats.free_count);
    PrintKernel(" frees\n");
    PrintKernel("  Fragmentation: ");
    PrintKernelInt(stats.fragmentation_score);
    PrintKernel("% (lower is better)\n");
    PrintKernel("  Largest free block: ");
    PrintKernelInt(stats.largest_free_block / 1024 / 1024);
    PrintKernel("MB\n\n");

    PrintVMemStats();
}

static char* GetArg(const char* cmd, int arg_num) {
    static char arg_buf[64];
    int word = 0, pos = 0, buf_pos = 0;
    
    // Skip leading spaces
    while (cmd[pos] == ' ') pos++;
    
    while (cmd[pos]) {
        if (cmd[pos] == ' ') {
            if (word == arg_num) {
                arg_buf[buf_pos] = '\0';
                if (buf_pos == 0) return NULL;
                char* copy = KernelMemoryAlloc(buf_pos + 1);
                if (!copy) return NULL;
                FastMemcpy(copy, arg_buf, buf_pos + 1);
                return copy;
            }
            // Skip multiple spaces
            while (cmd[pos] == ' ') pos++;
            word++;
            buf_pos = 0;
        } else {
            if (word == arg_num && buf_pos < 63) {
                arg_buf[buf_pos++] = cmd[pos];
            }
            pos++;
        }
    }
    
    // Handle last argument
    if (word == arg_num && buf_pos > 0) {
        arg_buf[buf_pos] = '\0';
        char* copy = KernelMemoryAlloc(buf_pos + 1);
        if (!copy) return NULL;
        FastMemcpy(copy, arg_buf, buf_pos + 1);
        return copy;
    }
    return NULL;
}

void ArpRequestTestProcess() {
    FullArpPacket packet;

    // Get the network card's info, especially our MAC address
    const Rtl8139Device* nic = GetRtl8139Device();
    if (!nic) {
        PrintKernelError("[NIC] RTL8139 not ready");
        return;
    }

    // --- Part 1: Build the Ethernet Header ---

    // Destination MAC: FF:FF:FF:FF:FF:FF (Broadcast)
    FastMemset(packet.eth.dest_mac, 0xFF, 6);

    // Source MAC: Our card's MAC address
    FastMemcpy(packet.eth.src_mac, nic->mac_address, 6);

    // EtherType: 0x0806 for ARP
    packet.eth.ethertype = HTONS(0x0806);

    // --- Part 2: Build the ARP Packet ---

    packet.arp.hardware_type = HTONS(1);      // 1 = Ethernet
    packet.arp.protocol_type = HTONS(0x0800); // 0x0800 = IPv4
    packet.arp.hardware_addr_len = 6;
    packet.arp.protocol_addr_len = 4;
    packet.arp.opcode = HTONS(1);             // 1 = ARP Request

    // Sender MAC: Our MAC address
    FastMemcpy(packet.arp.sender_mac, nic->mac_address, 6);

    // Sender IP: We don't have an IP stack, so let's pretend we're 192.168.1.100
    packet.arp.sender_ip[0] = 192;
    packet.arp.sender_ip[1] = 168;
    packet.arp.sender_ip[2] = 1;
    packet.arp.sender_ip[3] = 100;

    // Target MAC: 00:00:00:00:00:00 (this is what we're asking for)
    FastMemset(packet.arp.target_mac, 0x00, 6);

    // Target IP: The IP of the computer we want to find (e.g., your router)
    packet.arp.target_ip[0] = 192;
    packet.arp.target_ip[1] = 168;
    packet.arp.target_ip[2] = 1;
    packet.arp.target_ip[3] = 1;

    // --- Part 3: Send the packet! ---

    // The total packet size is the size of the two headers
    uint32_t packet_size = sizeof(EthernetHeader) + sizeof(ArpPacket);
    Rtl8139_SendPacket(&packet, packet_size);
}

static void ResolvePath(const char* input, char* output, int max_len) {
    if (!input || !output) return;
    
    if (input[0] == '/') {
        // Absolute path
        int len = 0;
        while (input[len] && len < max_len - 1) {
            output[len] = input[len];
            len++;
        }
        output[len] = '\0';
    } else {
        // Relative path - combine with current directory
        int curr_len = 0;
        while (current_dir[curr_len] && curr_len < max_len - 1) {
            output[curr_len] = current_dir[curr_len];
            curr_len++;
        }
        
        if (curr_len > 0 && current_dir[curr_len - 1] != '/' && curr_len < max_len - 1) {
            output[curr_len++] = '/';
        }
        
        int input_len = 0;
        while (input[input_len] && curr_len + input_len < max_len - 1) {
            output[curr_len + input_len] = input[input_len];
            input_len++;
        }
        output[curr_len + input_len] = '\0';
    }
}

static void show_help() {
    PrintKernelSuccess("VoidFrame Shell Commands:\n");
    PrintKernel("  help           - Show this help\n");
    PrintKernel("  ps             - List processes\n");
    PrintKernel("  sched          - Show scheduler state\n");
    PrintKernel("  perf           - Show performance stats\n");
    PrintKernel("  time           - Show current time\n");
    PrintKernel("  beep <x>       - Send Beep <x> times (SB16)\n");
    PrintKernel("  picmask <irq>  - Mask IRQ <irq>\n");
    PrintKernel("  picunmask <irq>- Unmask IRQ <irq>\n");
    PrintKernel("  perf           - Show performance stats\n");
    PrintKernel("  memstat        - Show memory statistics\n");
    PrintKernel("  serialw <msg>  - Write <msg> to available serial port\n");
    PrintKernel("  parallelw <msg>- Write <msg> to available parallel port\n");
    PrintKernel("  setfreq <hz>   - Set PIT timer <hz>\n");
    PrintKernel("  filesize <file>- Get size of <file> in bytes\n");
    PrintKernel("  lspci          - List current PCI device(s)\n");
    PrintKernel("  lsisa          - List current ISA device(s)\n");
    PrintKernel("  lsusb          - List current USB device(s) and xHCI controller(s)\n");
    PrintKernel("  arptest        - Perform an ARP test and send packets\n");
    PrintKernel("  elfload <path> - Load ELF executable in <path>\n");
    PrintKernel("  layoutmem      - Show current VoidFrame memory layout as of 14/08/25\n");
    PrintKernel("  vmemfreelist   - Show VMem free list\n");
    PrintKernel("  clear          - Clear screen\n");
    PrintKernel("  cd <dir>       - Change directory\n");
    PrintKernel("  pwd            - Print working directory\n");
    PrintKernel("  ls [path]      - List directory contents\n");
    PrintKernel("  cat <file>     - Display file contents\n");
    PrintKernel("  mkdir <name>   - Create directory\n");
    PrintKernel("  touch <name>   - Create empty file\n");
    PrintKernel("  alloc <size>   - Allocate <size> bytes\n");
    PrintKernel("  panic <message>- Panic with <message>\n");
    PrintKernel("  kill <pid>     - Terminate process with pid <pid>\n");
    PrintKernel("  rm <file>      - Remove file or empty directory\n");
    PrintKernel("  echo <text> <file> - Write text to file\n");
    PrintKernel("  fstest         - Run filesystem tests\n");
}

static void ExecuteCommand(const char* cmd) {
    char* cmd_name = GetArg(cmd, 0);
    if (!cmd_name) return;
    
    if (FastStrCmp(cmd_name, "help") == 0) {
        show_help();
    } else if (FastStrCmp(cmd_name, "ps") == 0) {
        ListProcesses();
    } else if (FastStrCmp(cmd_name, "perf") == 0) {
        DumpPerformanceStats();
    } else if (FastStrCmp(cmd_name, "layoutmem") == 0) {
        PrintKernelMemoryLayout();
    } else if (FastStrCmp(cmd_name, "lsisa") == 0) {
        IsaPrintDevices();
    } else if (FastStrCmp(cmd_name, "memstat") == 0) {
        MemoryStats stats;
        GetDetailedMemoryStats(&stats);
        PrintKernel("  Physical: ");
        PrintKernelInt(stats.free_physical_bytes / (1024*1024));
        PrintKernel("MB free, ");
        PrintKernelInt(stats.fragmentation_score);
        PrintKernel("% fragmented, Used: ");
        PrintKernelInt(stats.used_physical_bytes / (1024*1024));
        PrintKernel("MB\n");
        PrintVMemStats();
        PrintHeapStats();
    } else if (FastStrCmp(cmd_name, "lspci") == 0) {
        CreateProcess(PciEnumerate);
    } else if (FastStrCmp(cmd_name, "vmemfreelist") == 0) {
        VMemDumpFreeList();
    } else if (FastStrCmp(cmd_name, "lsusb") == 0) {
        CreateProcess(xHCIEnumerate);
    } else if (FastStrCmp(cmd_name, "alloc") == 0) {
        char* size_str = GetArg(cmd, 1);
        if (!size_str) {
            PrintKernel("Usage: alloc <size>\n");
            return;
        }
        int size = atoi(size_str);
        if (size <= 0) {
            PrintKernel("Usage: alloc <size>\n");
            KernelFree(size_str);
            return;
        }
        KernelMemoryAlloc((uint32_t)size);
    }  else if (FastStrCmp(cmd_name, "beep") == 0) {
        char* size_str = GetArg(cmd, 1);
        if (!size_str) {
            PrintKernel("Usage: beep <x>\n");
            return;
        }
        int size = atoi(size_str);
        if (size <= 0) {
            PrintKernel("Usage: beep <x>\n");
            return;
        }
        for (int i = 0; i < size; i++) {
            SB16_Beep(SB16_DSP_BASE);
        }
        KernelFree(size_str);
    } else if (FastStrCmp(cmd_name, "serialw") == 0) {
        char* str = GetArg(cmd, 1);
        if (!str) {
            PrintKernel("Usage: serialw <msg>\n");
            return;
        }
        if (SerialWrite(str) < 0) PrintKernelWarning("Serial write error\n");
        KernelFree(str);
    } else if (FastStrCmp(cmd_name, "parallelw") == 0) {
        char* str = GetArg(cmd, 1);
        if (!str) {
            PrintKernel("Usage: parallelw <msg>\n");
            return;
        }
        LPT_WriteString(str);
        KernelFree(str);
    } else if (FastStrCmp(cmd_name, "setfreq") == 0) {
        char* freq_str = GetArg(cmd, 1);
        if (!freq_str) {
            PrintKernel("Usage: setfreq <hz>\n");
            return;
        }
        const uint16_t freq = atoi(freq_str);
        if (freq <= 0) {
            PrintKernel("Usage: setfreq <hz>\n");
            return;
        }
        PitSetFrequency(freq);
    } else if (FastStrCmp(cmd_name, "panic") == 0) {
        char* str = GetArg(cmd, 1);
        if (!str) {
            PrintKernel("Usage: panic <message>\n");
            return;
        }
        PANIC(str);
    } else if (FastStrCmp(cmd_name, "kill") == 0) {
        char* pid_str = GetArg(cmd, 1);
        if (!pid_str) {
            PrintKernel("Usage: kill <pid>\n");
            return;
            }
        int pid = atoi(pid_str);
        KernelFree(pid_str);
        if (pid <= 0) {
            PrintKernel("Usage: kill <pid>\n");
            return;
        }
        KillProcess(pid);
    } else if (FastStrCmp(cmd_name, "picmask") == 0) {
        char* irq_str = GetArg(cmd, 1);
        if (!irq_str) {
            PrintKernel("Usage: picmask <irq>\n");
            return;
        }
        int irq = atoi(irq_str);
        KernelFree(irq_str);
        if (irq < 0 || irq > 15) {
            PrintKernel("Usage: picmask <irq>\n");
            return;
        }
        PIC_disable_irq(irq);
    } else if (FastStrCmp(cmd_name, "picunmask") == 0) {
        char* irq_str = GetArg(cmd, 1);
        if (!irq_str) {
            PrintKernel("Usage: picunmask <irq>\n");
            return;
        }
        int irq = atoi(irq_str);
        KernelFree(irq_str);
        if (irq < 0 || irq > 15) {
            PrintKernel("Usage: picunmask <irq>\n");
            return;
        }
        PIC_enable_irq(irq);
    } else if (FastStrCmp(cmd_name, "ver") == 0) {
        Version();
    } else if (FastStrCmp(cmd_name, "sched") == 0) {
        DumpSchedulerState();
    } else if (FastStrCmp(cmd_name, "clear") == 0) {
        ClearScreen();
    } else if (FastStrCmp(cmd_name, "cd") == 0) {
        char* dir = GetArg(cmd, 1);
        if (!dir) {
            FastMemcpy(current_dir, "/", 2);
            PrintKernel("[VFRFS] DIRECTORY SWITCHED TO /\n");
        } else {
            char new_path[256];
            ResolvePath(dir, new_path, 256);

            if (VfsIsDir(new_path)) {
                FastMemcpy(current_dir, new_path, 256);
                PrintKernel("VFS: DIRECTORY SWITCHED TO ");
                PrintKernel(current_dir);
                PrintKernel("\n");
            } else {
                PrintKernel("cd: no such directory: ");
                PrintKernel(new_path);
                PrintKernel("\n");
            }
            KernelFree(dir);
        }
    } else if (FastStrCmp(cmd_name, "time") == 0) {
        rtc_time_t current_time;
        RtcReadTime(&current_time);

        // Buffer for printing. Make it large enough.
        char time_str[64];
        char num_buf[5]; // For itoa

        // Format the date: YYYY-MM-DD
        itoa(current_time.year, num_buf);
        strcpy(time_str, num_buf);
        strcat(time_str, "-");

        if (current_time.month < 10) strcat(time_str, "0");
        itoa(current_time.month, num_buf);
        strcat(time_str, num_buf);
        strcat(time_str, "-");

        if (current_time.day < 10) strcat(time_str, "0");
        itoa(current_time.day, num_buf);
        strcat(time_str, num_buf);

        strcat(time_str, " "); // Separator

        // Format the time: HH:MM:SS
        if (current_time.hour < 10) strcat(time_str, "0");
        itoa(current_time.hour, num_buf);
        strcat(time_str, num_buf);
        strcat(time_str, ":");

        if (current_time.minute < 10) strcat(time_str, "0");
        itoa(current_time.minute, num_buf);
        strcat(time_str, num_buf);
        strcat(time_str, ":");

        if (current_time.second < 10) strcat(time_str, "0");
        itoa(current_time.second, num_buf);
        strcat(time_str, num_buf);

        PrintKernel(time_str);
        PrintKernel("\n");
    } else if (FastStrCmp(cmd_name, "pwd") == 0) {
        PrintKernel(current_dir);
        PrintKernel("\n");
    } else if (FastStrCmp(cmd_name, "arptest") == 0) {
        CreateProcess(ArpRequestTestProcess);
    } else if (FastStrCmp(cmd_name, "ls") == 0) {
        char* path = GetArg(cmd, 1);
        if (!path) {
            VfsListDir(current_dir);
        } else {
            char full_path[256];
            ResolvePath(path, full_path, 256);
            VfsListDir(full_path);
            KernelFree(path);
        }
    } else if (FastStrCmp(cmd_name, "cat") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);

            uint8_t* file_buffer = KernelMemoryAlloc(4096);
            if (file_buffer) {
                int bytes = VfsReadFile(full_path, file_buffer, 4095);
                if (bytes >= 0) {
                    // Null-terminate and print if non-empty
                    file_buffer[(bytes < 4095) ? bytes : 4095] = 0;
                    if (bytes > 0) {
                        PrintKernel((char*)file_buffer);
                    }
                    PrintKernel("\n");
                } else {
                    PrintKernel("cat: file not found or read error\n");
                }
                KernelFree(file_buffer);
            } else {
                PrintKernel("cat: out of memory\n");
            }
            KernelFree(file);
        } else {
            PrintKernel("Usage: cat <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "mkdir") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);
            if (VfsCreateDir(full_path) == 0) {
                PrintKernel("Directory created\n");
            } else {
                PrintKernel("Failed to create directory\n");
            }
            KernelFree(name);
        } else {
            PrintKernel("Usage: mkdir <dirname>\n");
        }
    } else if (FastStrCmp(cmd_name, "filesize") == 0) {
        char* filename = GetArg(cmd, 1);
        if (filename) {
            uint64_t size = VfsGetFileSize(filename);
            PrintKernel("File size: ");
            PrintKernelInt((uint32_t)size);
            PrintKernel(" bytes\n");
            KernelFree(filename);
        } else {
            PrintKernel("Usage: filesize <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "elfload") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);

            // Enhanced options for ELF loading
            ElfLoadOptions opts = {
                .privilege_level = PROC_PRIV_USER,
                .security_flags = 0,
                .max_memory = 16 * 1024 * 1024, // 16MB limit
                .process_name = full_path
            };

            uint32_t pid = CreateProcessFromElf(full_path, &opts);
            if (pid != 0) {
                PrintKernelSuccess("ELF Executable loaded (PID: ");
                PrintKernelInt(pid);
                PrintKernel(")\n");
            } else {
                PrintKernelError("Failed to load ELF executable\n");
            }
            KernelFree(name);
        } else {
            PrintKernel("Usage: elfload <path>\n");
        }
    } else if (FastStrCmp(cmd_name, "touch") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);
            if (VfsCreateFile(full_path) == 0) {
                PrintKernel("File created\n");
            } else {
                PrintKernel("Failed to create file\n");
            }
            KernelFree(name);
        } else {
            PrintKernel("Usage: touch <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "rm") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);
            if (VfsDelete(full_path) == 0) {
                PrintKernel("Removed\n");
            } else {
                PrintKernel("Failed to remove (file not found or directory not empty)\n");
            }
            KernelFree(name);
        } else {
            PrintKernel("Usage: rm <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "echo") == 0) {
        char* text = GetArg(cmd, 1);
        char* file = GetArg(cmd, 2);
        if (text && file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);
            int len = 0;
            while (text[len]) len++;
            if (VfsWriteFile(full_path, text, len) >= 0) {
                PrintKernel("Text written to file\n");
            } else {
                PrintKernel("Failed to write to file\n");
            }
            KernelFree(text);
            KernelFree(file);
        } else {
            PrintKernel("Usage: echo <text> <filename>\n");
            if (text) KernelFree(text);
            if (file) KernelFree(file);
        }
    } else if (FastStrCmp(cmd_name, "edit") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);
            EditorOpen(full_path);
            KernelFree(file);
        } else {
            PrintKernel("Usage: edit <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "fstest") == 0) {
        PrintKernel("VFS: Running filesystem tests...\n");
        
        if (VfsCreateDir("/test") == 0) {
            PrintKernel("VFS: Created /test directory\n");
        }
        
        char* test_text = "Hello VoidFrame!\n";
        int len = 0;
        while (test_text[len]) len++;
        if (VfsWriteFile("/test/hello.txt", test_text, len) >= 0) {
            PrintKernel("VFS: Created /test/hello.txt\n");
        }
        
        PrintKernel("VFS: Root directory contents:\n");
        VfsListDir("/");
        
        PrintKernel("VFS: Test directory contents:\n");
        VfsListDir("/test");
        
        PrintKernel("VFS: Contents of /test/hello.txt:\n");
        uint8_t* file_buffer = KernelMemoryAlloc(256);
        if (file_buffer) {
            int bytes = VfsReadFile("/test/hello.txt", file_buffer, 255);
            if (bytes > 0) {
                file_buffer[bytes] = 0;
                PrintKernel((char*)file_buffer);
            }
            KernelFree(file_buffer);
        }
        
        PrintKernel("VFS: Filesystem tests completed\n");
    } else {
        PrintKernel("Unknown command: ");
        PrintKernel(cmd_name);
        PrintKernel("\nType 'help' for commands\n");
    }
    
    KernelFree(cmd_name);
}

void ShellInit(void) {
    cmd_pos = 0;
}

void ShellProcess(void) {
    PrintKernelSuccess("System: VoidFrame Shell v0.0.1-beta\n");
    show_help();
    while (1) {
        if (HasInput()) {
            char c = GetChar();

            if (c == '\n') {
                PrintKernel("\n"); // Move to next line
                command_buffer[cmd_pos] = 0;
                ExecuteCommand(command_buffer);
                cmd_pos = 0;
                PrintKernel(current_dir);
                PrintKernel("> ");
            } else if (c == '\b') {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    PrintKernel("\b \b"); // Visual backspace
                }
            } else if (cmd_pos < 255) {
                command_buffer[cmd_pos++] = c;
                char str[2] = {c, 0};
                PrintKernel(str); // Echo character
            }
        } else {
            // Yield CPU when no input available
            Yield();
        }
    }
}