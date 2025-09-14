#include "InitRD.h"
#include "Multiboot2.h"
#include "Console.h"
#include "VFS.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VMem.h"

extern uint32_t g_multiboot_info_addr;

void InitRDLoad(void) {
    if (!g_multiboot_info_addr) {
        PrintKernelWarning("[INITRD] No multiboot info available\n");
        return;
    }

    PrintKernelF("[INITRD] Multiboot info at 0x%08X\n", g_multiboot_info_addr);
    uint32_t total_size = *(uint32_t*)g_multiboot_info_addr;
    PrintKernelF("[INITRD] Total size: %u bytes\n", total_size);

    struct MultibootTag* tag = (struct MultibootTag*)(g_multiboot_info_addr + 8);
    
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
            struct MultibootModuleTag* mod = (struct MultibootModuleTag*)tag;
            if (mod->mod_end <= mod->mod_start) {
                PrintKernelWarning("[INITRD] Invalid module range; skipping\n");
                continue;
            }
            uint32_t mod_size = mod->mod_end - mod->mod_start;
            
            PrintKernelF("[INITRD] Module: %s\n", mod->cmdline);
            PrintKernelF("[INITRD] Start: 0x%08X, End: 0x%08X, Size: %u\n", 
                        mod->mod_start, mod->mod_end, mod_size);
            
            if (mod_size == 0 || mod_size > 16*1024*1024) {
                PrintKernelF("[INITRD] Invalid module size, skipping\n");
                continue;
            }
            
            // Map the module's physical range into kernel virtual space temporarily
            uint64_t paddr_start = (uint64_t)mod->mod_start;
            uint64_t paddr_end   = (uint64_t)mod->mod_end;
            uint64_t paddr_len   = paddr_end - paddr_start;

            uint64_t aligned_paddr = PAGE_ALIGN_DOWN(paddr_start);
            uint64_t offset        = paddr_start - aligned_paddr;
            uint64_t map_size      = PAGE_ALIGN_UP(offset + paddr_len);

            void* temp_vaddr = VMemAlloc(map_size);
            if (!temp_vaddr) {
                PrintKernelF("[INITRD] Failed to allocate temp vaddr for module %s\n", mod->cmdline);
                continue;
            }

            // Unmap the RAM pages that VMemAlloc mapped before remapping to module phys
            int unmap_res = VMemUnmap((uint64_t)temp_vaddr, map_size);
            if (unmap_res != VMEM_SUCCESS) {
                PrintKernelF("[INITRD] Failed to unmap temp vaddr before MMIO map: %d\n", unmap_res);
                VMemFree(temp_vaddr, map_size);
                continue;
            }

            int map_res = VMemMapMMIO((uint64_t)temp_vaddr, aligned_paddr, map_size, PAGE_WRITABLE);
            if (map_res != VMEM_SUCCESS) {
                PrintKernelF("[INITRD] Failed to map module phys -> virt: %d\n", map_res);
                VMemFree(temp_vaddr, map_size);
                continue;
            }

            uint8_t* mod_data = (uint8_t*)temp_vaddr + offset;

            // Validate data is readable
            if (mod_size >= 3 && mod_data[0] == 0 && mod_data[1] == 0 && mod_data[2] == 0) {
                PrintKernelWarning("[INITRD] Module data appears to be zeroed\n");
            }
            
            // Debug: show first few bytes
            PrintKernelF("[INITRD] First 16 bytes: ");
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
            PrintKernelF("[INITRD] Data type: %s\n", is_text ? "Text" : "Binary");
            
            if (VfsWriteFile(mod->cmdline, mod_data, mod_size) >= 0) {
                PrintKernelF("[INITRD] Copied %s to VFS\n", mod->cmdline);
            } else {
                PrintKernelF("[INITRD] Failed to copy %s\n", mod->cmdline);
            }

            // Unmap and free temporary mapping
            VMemUnmapMMIO((uint64_t)temp_vaddr, map_size);
            VMemFree(temp_vaddr, map_size);
        }
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
}