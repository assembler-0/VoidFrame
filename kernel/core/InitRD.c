#include "InitRD.h"
#include "Multiboot2.h"
#include "Console.h"
#include "VFS.h"
extern uint32_t g_multiboot_info_addr;

void InitRDLoad(void) {
    if (!g_multiboot_info_addr) {
        PrintKernelWarning("InitRD: No multiboot info available\n");
        return;
    }

    PrintKernelF("InitRD: Multiboot info at 0x%08X\n", g_multiboot_info_addr);
    uint32_t total_size = *(uint32_t*)g_multiboot_info_addr;
    PrintKernelF("InitRD: Total size: %u bytes\n", total_size);
    
    struct MultibootTag* tag = (struct MultibootTag*)(g_multiboot_info_addr + 8);
    
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
            struct MultibootModuleTag* mod = (struct MultibootModuleTag*)tag;
            uint32_t mod_size = mod->mod_end - mod->mod_start;
            
            PrintKernelF("InitRD: Module: %s\n", mod->cmdline);
            PrintKernelF("InitRD: Start: 0x%08X, End: 0x%08X, Size: %u\n", 
                        mod->mod_start, mod->mod_end, mod_size);
            
            if (mod_size == 0 || mod_size > 16*1024*1024) {
                PrintKernelF("InitRD: Invalid module size, skipping\n");
                continue;
            }
            
            // Map physical address to virtual (identity mapped in lower 4GB)
            uint8_t* mod_data = (uint8_t*)(uintptr_t)mod->mod_start;
            
            // Validate data is readable
            if (mod_data[0] == 0 && mod_data[1] == 0 && mod_data[2] == 0) {
                PrintKernelWarning("InitRD: Module data appears to be zeroed\n");
            }
            
            // Debug: show first few bytes
            PrintKernelF("InitRD: First 16 bytes: ");
            for (int i = 0; i < 16 && i < mod_size; i++) {
                PrintKernelF("%02X ", mod_data[i]);
            }
            PrintKernel("\n");
            
            // Check if it looks like text
            bool is_text = true;
            for (int i = 0; i < 32 && i < mod_size; i++) {
                if (mod_data[i] < 32 && mod_data[i] != '\n' && mod_data[i] != '\r' && mod_data[i] != '\t') {
                    is_text = false;
                    break;
                }
            }
            PrintKernelF("InitRD: Data type: %s\n", is_text ? "Text" : "Binary");
            
            if (VfsWriteFile(mod->cmdline, mod_data, mod_size) >= 0) {
                PrintKernelF("InitRD: Copied %s to VFS\n", mod->cmdline);
            } else {
                PrintKernelF("InitRD: Failed to copy %s\n", mod->cmdline);
            }
        }
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
}