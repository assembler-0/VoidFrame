#include "Shell.h"
#include "ELFloader.h"
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
#include "stdlib.h"
#include "xHCI/xHCI.h"
#include "FsUtils.h"

#define DATE __DATE__
#define TIME __TIME__

static char command_buffer[256];
static int cmd_pos = 0;
static char current_dir[256] = "/";

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

typedef void (*ShellCommandFunc)(const char* args);

typedef struct {
    const char * name;
    ShellCommandFunc func;
} ShellCommand;

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

static void ResolvePath(const char* input, char* output, int max_len) {
    ResolvePathH(current_dir, input, output, max_len);
}

void ArpRequestTestProcess() {
    FullArpPacket packet;

    const Rtl8139Device* nic = GetRtl8139Device();
    if (!nic) {
        PrintKernelError("RTL8139 not ready");
        return;
    }

    FastMemset(packet.eth.dest_mac, 0xFF, 6);
    FastMemcpy(packet.eth.src_mac, nic->mac_address, 6);
    packet.eth.ethertype = HTONS(0x0806);
    packet.arp.hardware_type = HTONS(1);      // 1 = Ethernet
    packet.arp.protocol_type = HTONS(0x0800); // 0x0800 = IPv4
    packet.arp.hardware_addr_len = 6;
    packet.arp.protocol_addr_len = 4;
    packet.arp.opcode = HTONS(1);
    FastMemcpy(packet.arp.sender_mac, nic->mac_address, 6);
    packet.arp.sender_ip[0] = 192;
    packet.arp.sender_ip[1] = 168;
    packet.arp.sender_ip[2] = 1;
    packet.arp.sender_ip[3] = 100;

    FastMemset(packet.arp.target_mac, 0x00, 6);
    packet.arp.target_ip[0] = 192;
    packet.arp.target_ip[1] = 168;
    packet.arp.target_ip[2] = 1;
    packet.arp.target_ip[3] = 1;

    uint32_t packet_size = sizeof(EthernetHeader) + sizeof(ArpPacket);
    Rtl8139_SendPacket(&packet, packet_size);
}

static void ClearHandler(const char * args) {
    (void)args;
    ClearScreen();
}

static void ARPTestHandler(const char * args) {
    (void)args;
    CreateProcess(ArpRequestTestProcess);
}

static void VersionHandler(const char * args) {
    (void)args;
    PrintKernelSuccess("VoidFrame v0.0.1-beta5\n");
    PrintKernelF("Built on %s at %s\n", DATE, TIME);
    PrintKernelSuccess("VoidFrame Shell v0.0.1-beta\n");
}

static void HelpHandler(const char * args) {
    (void)args;
    PrintKernelSuccess("VoidFrame Shell Commands:\n");
    PrintKernel("  help           - Show this help\n");
    PrintKernel("  ps             - List processes\n");
    PrintKernel("  sched          - Show scheduler state\n");
    PrintKernel("  perf           - Show performance stats\n");
    PrintKernel("  time           - Show current time\n");
    PrintKernel("  edit <file>    - Edit <file>\n");
    PrintKernel("  beep <x>       - Send Beep <x> times (SB16)\n");
    PrintKernel("  picmask <irq>  - Mask IRQ <irq>\n");
    PrintKernel("  picunmask <irq>- Unmask IRQ <irq>\n");
    PrintKernel("  memstat        - Show memory statistics\n");
    PrintKernel("  serialw <msg>  - Write <msg> to available serial port\n");
    PrintKernel("  parallelw <msg>- Write <msg> to available parallel port\n");
    PrintKernel("  setfreq <hz>   - Set PIT timer <hz>\n");
    PrintKernel("  filesize <file>- Get size of <file> in bytes\n");
    PrintKernel("  lspci          - List current PCI device(s)\n");
    PrintKernel("  lsisa          - List current ISA device(s)\n");
    PrintKernel("  lsusb          - List current USB device(s) and xHCI controller(s)\n");
    PrintKernel("  arptest        - Perform an ARP test and send packets\n");
    PrintKernel("  elfload <path> - Load an ELF executable in <path>\n");
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
    PrintKernel("  size <file>    - Get size of <file> in bytes\n");
    PrintKernel("  heapvallvl <0/1/2>- Set kernel heap validation level\n");
    PrintKernel("  lscpu          - List cpu features (CPUID)\n");
}

static void PSHandler(const char * args) {
    (void)args;
    ListProcesses();
}

static void PerfHandler(const char * args) {
    (void)args;
    DumpPerformanceStats();
}

static void SchedHandler(const char * args) {
    (void)args;
    DumpSchedulerState();
}

static void LsISAHandler(const char * args) {
    (void)args;
    IsaPrintDevices();
}

static void MemstatHandler(const char * args) {
    (void)args;
    MemoryStats stats;
    GetDetailedMemoryStats(&stats);
    PrintKernel("  Physical: ");
    PrintKernelInt(stats.free_physical_bytes / (1024*1024));
    PrintKernel("MB free, ");
    PrintKernelInt(stats.fragmentation_score);
    PrintKernel("% fragmented, Used: ");
    PrintKernelInt(stats.used_physical_bytes / (1024*1024));
    PrintKernel("MB\n");
}

static void LsPCIHandler(const char * args) {
    (void)args;
    CreateProcess(PciEnumerate);
}

static void VmemFreeListHandler(const char * args) {
    (void)args;
    VMemDumpFreeList();
}

static void AllocHandler(const char * args) {
    char* size_str = GetArg(args, 1);
    if (!size_str) {
        PrintKernel("Usage: alloc <size>\n");
        KernelFree(size_str);
        return;
    }
    int size = atoi(size_str);
    if (size <= 0) {
        PrintKernel("Usage: alloc <size>\n");
        KernelFree(size_str);
        return;
    }
    KernelMemoryAlloc((uint32_t)size);
    KernelFree(size_str);
}

static void PanicHandler(const char * args) {
    char* str = GetArg(args, 1);
    if (!str) {
        PrintKernel("Usage: panic <message>\n");
        KernelFree(str);
        return;
    }
    PANIC(str);
}

static void LsUSBHandler(const char * args) {
    CreateProcess(xHCIEnumerate);
}

static void BeepHandler(const char * args) {
    char* size_str = GetArg(args, 1);
    if (!size_str) {
        PrintKernel("Usage: beep <x>\n");
        KernelFree(size_str);
        return;
    }
    int size = atoi(size_str);
    if (size <= 0) {
        PrintKernel("Usage: beep <x>\n");
        KernelFree(size_str);
        return;
    }
    for (int i = 0; i < size; i++) {
        SB16_Beep(SB16_DSP_BASE);
    }
    KernelFree(size_str);
}

static void SerialWHandler(const char * args) {
    char* str = GetArg(args, 1);
    if (!str) {
        PrintKernel("Usage: serialw <msg>\n");
        KernelFree(str);
        return;
    }
    if (SerialWrite(str) < 0) PrintKernelWarning("Serial write error\n");
    KernelFree(str);
}

static void ParallelWHandler(const char * args) {
    char* str = GetArg(args, 1);
    if (!str) {
        PrintKernel("Usage: parallelw <msg>\n");
        KernelFree(str);
        return;
    }
    LPT_WriteString(str);
    KernelFree(str);
}

static void SetfreqHandler(const char * args) {
    char* freq_str = GetArg(args, 1);
    if (!freq_str) {
        PrintKernel("Usage: setfreq <hz>\n");
        KernelFree(freq_str);
        return;
    }
    int freq_i = atoi(freq_str);
    if (freq_i < 19 || freq_i > 65535) {
        PrintKernel("Usage: setfreq <hz>\n");
        KernelFree(freq_str);
        return;
    }
    PitSetFrequency((uint16_t)freq_i);
    KernelFree(freq_str);
}

static void KillHandler(const char * args) {
    char* pid_str = GetArg(args, 1);
    if (!pid_str) {
        PrintKernel("Usage: kill <pid>\n");
        KernelFree(pid_str);
        return;
    }
    int pid = atoi(pid_str);
    KernelFree(pid_str);
    if (pid <= 0) {
        PrintKernel("Usage: kill <pid>\n");
        return;
    }
    KillProcess(pid);
}

static void PicmaskHandler(const char * args) {
    char* irq_str = GetArg(args, 1);
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
}

static void PicunmaskHandler(const char * args) {
    char* irq_str = GetArg(args, 1);
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
}

static void CdHandler(const char * args) {
    char* dir = GetArg(args, 1);
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
}

static void TimeHandler(const char * args) {
    (void)args;
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
}

static void PwdHandler(const char * args) {
    (void)args;
    PrintKernel(current_dir);
    PrintNewline();
}

static void LsHandler(const char * args) {
    char * path = GetArg(args, 1);
    if (!path) {
        VfsListDir(current_dir);
    } else {
        char full_path[256];
        ResolvePath(path, full_path, 256);
        VfsListDir(full_path);
    }
    if (path) KernelFree(path);
}

static void CatHandler(const char * args) {
    char* file = GetArg(args, 1);
    if (!file) {
        PrintKernel("Usage: cat <filename>\n");
        KernelFree(file);
        return;
    }
    char full_path[256];
    ResolvePath(file, full_path, 256);
    uint8_t* file_buffer = KernelMemoryAlloc(4096);
    if (!file_buffer) {
        PrintKernel("cat: out of memory\n");
        KernelFree(file);
        return;
    }
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
    KernelFree(file);
}

static void MkdirHandler(const char * args) {
    char* name = GetArg(args, 1);
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
        KernelFree(name);
    }
}

static void FileSizeHandler(const char * args) {
    char* filename = GetArg(args, 1);
    if (filename) {
        char full_path[256];
        ResolvePath(filename, full_path, 256);
        const uint64_t size = VfsGetFileSize(full_path);
        PrintKernel("File size: ");
        PrintKernelInt((uint32_t)size);
        PrintKernel(" bytes\n");
        KernelFree(filename);
    } else {
        PrintKernel("Usage: filesize <filename>\n");
        KernelFree(filename);
    }
}

static void ElfloadHandler(const char * args) {
    char* name = GetArg(args, 1);
    if (name) {
        char full_path[256];
        ResolvePath(name, full_path, 256);

        const ElfLoadOptions opts = {
            .privilege_level = PROC_PRIV_USER,
            .security_flags = 0,
            .max_memory = 16 * 1024 * 1024,
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
        KernelFree(name);
    }
}

static void TouchHandler(const char * args) {
    char* name = GetArg(args, 1);
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
        KernelFree(name);
    }
}

static void RmHandler(const char * args) {
    char* name = GetArg(args, 1);
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
        KernelFree(name);
    }
}

static void EchoHandler(const char * args) {
    char* text = GetArg(args, 1);
    char* file = GetArg(args, 2);
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
}

static void EditHandler(const char * args) {
    char* file = GetArg(args, 1);
    if (file) {
        char full_path[256];
        ResolvePath(file, full_path, 256);
        EditorOpen(full_path);
        KernelFree(file);
    } else {
        PrintKernel("Usage: edit <filename>\n");
        KernelFree(file);
    }
}

static void FstestHandler(const char * args) {
    (void)args;
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
}

static void SizeHandler(const char * args) {
    char* name = GetArg(args, 1);
    if (name) {
        char full_path[256];
        ResolvePath(name, full_path, 256);
        const uint64_t size = VfsGetFileSize(full_path);
        PrintKernelInt((uint32_t)size);
        PrintKernel(" bytes\n");
        KernelFree(name);
    } else {
        PrintKernel("Usage: size <filename>\n");
        KernelFree(name);
    }
}

static void KHeapValidationHandler(const char * args) {
    char* lvl_str = GetArg(args, 1);
    if (!lvl_str) {
        PrintKernel("Usage: heapvallvl <0/1/2>\n");
        KernelFree(lvl_str);
        return;
    }
    int lvl = atoi(lvl_str);
    if (lvl > 2 || lvl < 0) {
        PrintKernel("Usage: heapvallvl <size>\n");
        KernelFree(lvl_str);
        return;
    }
    KernelFree(lvl_str);
    switch (lvl) {
        case 0: KernelHeapSetValidationLevel(KHEAP_VALIDATION_NONE); break;
        case 1: KernelHeapSetValidationLevel(KHEAP_VALIDATION_BASIC); break;
        case 2: KernelHeapSetValidationLevel(KHEAP_VALIDATION_FULL); break;
        default: __builtin_unreachable(); break;
    }
}

// Function to print the CPU vendor string
void PrintVendorString() {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    cpuid(0, &eax, &ebx, &ecx, &edx);
    *(uint32_t*)(vendor) = ebx;
    *(uint32_t*)(vendor + 4) = edx;
    *(uint32_t*)(vendor + 8) = ecx;
    vendor[12] = '\0';
    PrintKernelF("Vendor ID:                       %s\n", vendor);
}

// Function to print the CPU brand string
void PrintBrandString() {
    uint32_t eax, ebx, ecx, edx;
    char brand[49];

    // Check for extended function support
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        cpuid(0x80000002, (uint32_t*)(brand), (uint32_t*)(brand + 4), (uint32_t*)(brand + 8), (uint32_t*)(brand + 12));
        cpuid(0x80000003, (uint32_t*)(brand + 16), (uint32_t*)(brand + 20), (uint32_t*)(brand + 24), (uint32_t*)(brand + 28));
        cpuid(0x80000004, (uint32_t*)(brand + 32), (uint32_t*)(brand + 36), (uint32_t*)(brand + 40), (uint32_t*)(brand + 44));
        brand[48] = '\0';
        PrintKernelF("Model name:                      %s\n", brand);
    }
}

// Function to print CPU family, model, and stepping
void PrintCpuInfo() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);

    uint32_t family = (eax >> 8) & 0xF;
    uint32_t model = (eax >> 4) & 0xF;
    uint32_t stepping = eax & 0xF;

    if (family == 0x6 || family == 0xF) {
        uint32_t ext_family = (eax >> 20) & 0xFF;
        family += ext_family;
    }
    if (family == 0x6 || family == 0xF) {
        uint32_t ext_model = (eax >> 16) & 0xF;
        model += (ext_model << 4);
    }

    PrintKernelF("CPU family:                      %u\n", family);
    PrintKernelF("Model:                           %u\n", model);
    PrintKernelF("Stepping:                        %u\n", stepping);
}

// Function to print CPU features
void PrintFeatures() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);

    PrintKernelF("Flags:                           ");
    if (edx & (1 << 0)) PrintKernelF("fpu ");
    if (edx & (1 << 4)) PrintKernelF("tsc ");
    if (edx & (1 << 8)) PrintKernelF("cx8 ");
    if (edx & (1 << 15)) PrintKernelF("cmov ");
    if (edx & (1 << 19)) PrintKernelF("clflush ");
    if (edx & (1 << 23)) PrintKernelF("mmx ");
    if (edx & (1 << 24)) PrintKernelF("fxsr ");
    if (edx & (1 << 25)) PrintKernelF("sse ");
    if (edx & (1 << 26)) PrintKernelF("sse2 ");
    if (edx & (1 << 28)) PrintKernelF("htt ");

    if (ecx & (1 << 0)) PrintKernelF("sse3 ");
    if (ecx & (1 << 9)) PrintKernelF("ssse3 ");
    if (ecx & (1 << 19)) PrintKernelF("sse4_1 ");
    if (ecx & (1 << 20)) PrintKernelF("sse4_2 ");
    if (ecx & (1 << 23)) PrintKernelF("popcnt ");
    if (ecx & (1 << 25)) PrintKernelF("aes ");
    if (ecx & (1 << 28)) PrintKernelF("avx ");

    // Check for extended features
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000001) {
        cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 29)) PrintKernelF("lm "); // Long Mode (x86-64)
    }

    PrintKernelF("\n");
}

// Function to print cache information
void PrintCacheInfo() {
    uint32_t eax, ebx, ecx, edx;
    for (int i = 0; ; ++i) {
        cpuid(4, &eax, &ebx, &ecx, &edx);
        eax = i; // Subleaf
        uint32_t cache_type = eax & 0x1F;

        if (cache_type == 0) {
            break;
        }

        uint32_t cache_level = (eax >> 5) & 0x7;
        uint32_t ways = ((ebx >> 22) & 0x3FF) + 1;
        uint32_t partitions = ((ebx >> 12) & 0x3FF) + 1;
        uint32_t line_size = (ebx & 0xFFF) + 1;
        uint32_t sets = ecx + 1;
        uint32_t cache_size = ways * partitions * line_size * sets;

        const char* type_str = "Unknown";
        if(cache_type == 1) type_str = "Data";
        if(cache_type == 2) type_str = "Instruction";
        if(cache_type == 3) type_str = "Unified";

        PrintKernelF("L%u cache (%s):                 %u KB\n", cache_level, type_str, cache_size / 1024);
    }
}

// Function to print CPU topology
void PrintTopology() {
    uint32_t eax, ebx, ecx, edx;
    uint32_t threads_per_core = 1;
    uint32_t cores_per_socket = 1;

    // Check for Hyper-Threading
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (edx & (1 << 28)) {
        threads_per_core = (ebx >> 16) & 0xFF;
    }

    // Modern topology enumeration (Leaf 0xB)
    cpuid(0xB, &eax, &ebx, &ecx, &edx);
    if (ebx != 0) { // Check if leaf 0xB is supported
        // Iterate through levels to find core and thread information
        // This is a simplified approach. A full implementation would parse the x2APIC ID shifts.
        // For simplicity, we'll rely on older methods for this example.
    } else {
        // Fallback for older CPUs
        cpuid(4, &eax, &ebx, &ecx, &edx);
        if (eax != 0) { // Check if leaf 4 is supported
            cores_per_socket = ((eax >> 26) & 0x3F) + 1;
        }
    }


    PrintKernelF("Thread(s) per core:              %u\n", threads_per_core);
    PrintKernelF("Core(s) per socket:              %u\n", cores_per_socket);
    PrintKernelF("Socket(s):                       1\n"); // Assuming a single socket for simplicity
}

void PrintCpuFrequency() {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x16, &eax, &ebx, &ecx, &edx);
    if (eax != 0) {
        PrintKernelF("CPU max MHz:                     %u\n", ebx);
        PrintKernelF("CPU base MHz:                    %u\n", eax);
        PrintKernelF("Bus (reference) MHz:             %u\n", ecx);
    }
}

    
// The main shell command function
void LsCPUHandler(const char* args) {
    (void)args;
    PrintVendorString();
    PrintBrandString();
    PrintCpuInfo();
    PrintTopology();
    PrintCpuFrequency();
    PrintCacheInfo();
    PrintFeatures();
}

static const ShellCommand commands[] = {
    {"help", HelpHandler},
    {"ps", PSHandler},
    {"sched", SchedHandler},
    {"perf", PerfHandler},
    {"time", TimeHandler},
    {"beep", BeepHandler},
    {"picmask", PicmaskHandler},
    {"picunmask", PicunmaskHandler},
    {"memstat", MemstatHandler},
    {"serialw", SerialWHandler},
    {"parallelw", ParallelWHandler},
    {"setfreq", SetfreqHandler},
    {"filesize", FileSizeHandler},
    {"lspci", LsPCIHandler},
    {"lsusb", LsUSBHandler},
    {"lsisa", LsISAHandler},
    {"arptest", ARPTestHandler},
    {"elfload", ElfloadHandler},
    {"vmemfreelist", VmemFreeListHandler},
    {"clear", ClearHandler},
    {"cd", CdHandler},
    {"pwd", PwdHandler},
    {"ls", LsHandler},
    {"cat", CatHandler},
    {"mkdir", MkdirHandler},
    {"touch", TouchHandler},
    {"alloc", AllocHandler},
    {"panic", PanicHandler},
    {"kill", KillHandler},
    {"rm", RmHandler},
    {"echo", EchoHandler},
    {"edit", EditHandler},
    {"ver", VersionHandler},
    {"fstest", FstestHandler},
    {"size", SizeHandler},
    {"heapvallvl", KHeapValidationHandler},
    {"lscpu", LsCPUHandler},
};

static void ExecuteCommand(const char* cmd) {
    char* cmd_name = GetArg(cmd, 0);
    if (!cmd_name) return;

    for (size_t i = 0; i < (sizeof(commands) / sizeof(ShellCommand)); i++) {
        if (FastStrCmp(cmd_name, commands[i].name) == 0) {
            commands[i].func(cmd); // Call the handler
            KernelFree(cmd_name);
            return; // Command found and executed
        }
    }
    PrintKernel("Unknown command: ");
    PrintKernel(cmd_name);
    PrintKernel("\nType 'help' for commands\n");
    
    KernelFree(cmd_name);
}

void ShellInit(void) {
    cmd_pos = 0;
}

void ShellProcess(void) {
    PrintKernelSuccess("System: VoidFrame Shell v0.0.1-beta ('help' for list of commands)\n");
    while (1) {
        if (HasInput()) {
            const char c = GetChar();

            if (c == '\n') {
                PrintNewline();
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
                const char str[2] = {c, 0};
                PrintKernel(str); // Echo character
            }
        } else {
            Yield();
        }
    }
}