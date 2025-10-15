#include "NVMe.h"
#include "Console.h"
#include "TSC.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VMem.h"
#include "BlockDevice.h"
#include "Format.h"
#include "PCI/PCI.h"

static NVMeController g_nvme_controller = {0};

// Forward declarations
static int NVMe_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer);
static int NVMe_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer);

static uint32_t NVMe_ReadReg32(uint32_t offset) {
    return *(volatile uint32_t*)(g_nvme_controller.mmio_base + offset);
}

static void NVMe_WriteReg32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(g_nvme_controller.mmio_base + offset) = value;
}

static uint64_t NVMe_ReadReg64(uint32_t offset) {
    return *(volatile uint64_t*)(g_nvme_controller.mmio_base + offset);
}

static void NVMe_WriteReg64(uint32_t offset, uint64_t value) {
    *(volatile uint64_t*)(g_nvme_controller.mmio_base + offset) = value;
}

static int NVMe_WaitReady(int ready) {
    int timeout = 5000;
    while (timeout-- > 0) {
        uint32_t csts = NVMe_ReadReg32(NVME_CSTS);
        if (ready && (csts & NVME_CSTS_RDY)) return 0;
        if (!ready && !(csts & NVME_CSTS_RDY)) return 0;
        delay_us(1000);
    }
    return -1;
}

static int NVMe_SubmitAdminCommand(NVMeSubmissionEntry* cmd) {
    NVMeController* ctrl = &g_nvme_controller;
    
    // Copy command to submission queue
    FastMemcpy(&ctrl->admin_sq[ctrl->admin_sq_tail], cmd, sizeof(NVMeSubmissionEntry));
    
    // Ensure the SQE is visible to the device before ringing the doorbell
    __asm__ volatile("mfence" ::: "memory");
    
    // Update tail pointer
    ctrl->admin_sq_tail = (ctrl->admin_sq_tail + 1) % NVME_ADMIN_QUEUE_SIZE;
    
    // Ring doorbell for Admin SQ (QID 0)
    uint32_t db_shift = 2 + ctrl->dstrd;
    uint32_t asq_db_off = 0x1000 + ((0 * 2 + 0) << db_shift);
    NVMe_WriteReg32(asq_db_off, ctrl->admin_sq_tail); // Admin SQ doorbell
    
    // Wait for completion
    int timeout = 5000;
    while (timeout-- > 0) {
        NVMeCompletionEntry* cqe = &ctrl->admin_cq[ctrl->admin_cq_head];
        uint16_t status = cqe->status;
        
        if ((status & 1) == ctrl->admin_cq_phase) {
            // Command completed
            ctrl->admin_cq_head = (ctrl->admin_cq_head + 1) % NVME_ADMIN_QUEUE_SIZE;
            if (ctrl->admin_cq_head == 0) {
                ctrl->admin_cq_phase = !ctrl->admin_cq_phase;
            }
            
            // Ring completion doorbell for Admin CQ (QID 0)
            uint32_t acq_db_off = 0x1000 + ((0 * 2 + 1) << db_shift);
            NVMe_WriteReg32(acq_db_off, ctrl->admin_cq_head); // Admin CQ doorbell
            
            return (status >> 1) & 0x7FF; // Return status code
        }
        delay_us(100);
    }
    
    return -1; // Timeout
}

static int NVMe_SubmitIOCommand(NVMeSubmissionEntry* cmd) {
    NVMeController* ctrl = &g_nvme_controller;
    
    // Copy command to I/O submission queue
    FastMemcpy(&ctrl->io_sq[ctrl->io_sq_tail], cmd, sizeof(NVMeSubmissionEntry));
    
    // Ensure the SQE is visible to the device before ringing the doorbell
    __asm__ volatile("mfence" ::: "memory");
    
    // Update tail pointer
    ctrl->io_sq_tail = (ctrl->io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
    
    // Ring doorbell (I/O queue 1)
    uint32_t db_shift = 2 + ctrl->dstrd;
    uint32_t iosq_db_off = 0x1000 + ((1 * 2 + 0) << db_shift);
    NVMe_WriteReg32(iosq_db_off, ctrl->io_sq_tail); // I/O SQ doorbell
    
    // Wait for completion
    int timeout = 5000;
    while (timeout-- > 0) {
        NVMeCompletionEntry* cqe = &ctrl->io_cq[ctrl->io_cq_head];
        uint16_t status = cqe->status;
        
        if ((status & 1) == ctrl->io_cq_phase) {
            // Command completed
            ctrl->io_cq_head = (ctrl->io_cq_head + 1) % NVME_IO_QUEUE_SIZE;
            if (ctrl->io_cq_head == 0) {
                ctrl->io_cq_phase = !ctrl->io_cq_phase;
            }
            
            // Ring completion doorbell (I/O queue 1)
            uint32_t iocq_db_off = 0x1000 + ((1 * 2 + 1) << db_shift);
            NVMe_WriteReg32(iocq_db_off, ctrl->io_cq_head); // I/O CQ doorbell
            
            return (status >> 1) & 0x7FF; // Return status code
        }
        delay_us(50);
    }
    
    return -1; // Timeout
}

static int NVMe_CreateIOQueues(void) {
    NVMeController* ctrl = &g_nvme_controller;
    
    // Allocate I/O queues
    ctrl->io_sq = (NVMeSubmissionEntry*)VMemAlloc(NVME_IO_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    ctrl->io_cq = (NVMeCompletionEntry*)VMemAlloc(NVME_IO_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    
    if (!ctrl->io_sq || !ctrl->io_cq) {
        PrintKernel("NVMe: Failed to allocate I/O queues\n");
        return -1;
    }
    
    FastMemset(ctrl->io_sq, 0, NVME_IO_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    FastMemset(ctrl->io_cq, 0, NVME_IO_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    
    ctrl->io_sq_phys = VMemGetPhysAddr((uint64_t)ctrl->io_sq);
    ctrl->io_cq_phys = VMemGetPhysAddr((uint64_t)ctrl->io_cq);
    ctrl->io_sq_tail = 0;
    ctrl->io_cq_head = 0;
    ctrl->io_cq_phase = 1;
    
    // Create I/O Completion Queue
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = 0x05 | ((uint32_t)(++ctrl->next_cid) << 16); // CREATE_IO_CQ opcode
    cmd.prp1 = ctrl->io_cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1; // Queue size and ID
    cmd.cdw11 = 1; // Physically contiguous
    
    if (NVMe_SubmitAdminCommand(&cmd) != 0) {
        PrintKernel("NVMe: Failed to create I/O completion queue\n");
        return -1;
    }
    
    // Create I/O Submission Queue
    FastMemset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x01 | ((uint32_t)(++ctrl->next_cid) << 16); // CREATE_IO_SQ opcode
    cmd.prp1 = ctrl->io_sq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1; // Queue size and ID
    cmd.cdw11 = (1 << 16) | 1; // CQ ID and physically contiguous
    
    if (NVMe_SubmitAdminCommand(&cmd) != 0) {
        PrintKernel("NVMe: Failed to create I/O submission queue\n");
        return -1;
    }
    
    return 0;
}

static uint64_t NVMe_GetNamespaceSize(void) {
    // Allocate buffer for identify data
    uint8_t* identify_data = (uint8_t*)KernelMemoryAlloc(4096);
    if (!identify_data) return 0;
    
    FastMemset(identify_data, 0, 4096);
    uint64_t identify_phys = VMemGetPhysAddr((uint64_t)identify_data);
    
    // Send IDENTIFY command for namespace 1
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = NVME_ADMIN_IDENTIFY | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1; // Namespace 1
    cmd.prp1 = identify_phys;
    cmd.cdw10 = 0; // Identify namespace
    
    int result = NVMe_SubmitAdminCommand(&cmd);
    if (result != 0) {
        KernelFree(identify_data);
        return 0x1000000; // Fallback size
    }
    
    // Get namespace size from identify data (bytes 0-7)
    uint64_t nsze = *(uint64_t*)identify_data;
    
    KernelFree(identify_data);
    
    return nsze ? nsze : 0x1000000; // Return size or fallback
}

int NVMe_ReadSectors(uint64_t lba, uint16_t count, void* buffer) {
    if (!g_nvme_controller.initialized) return -1;

    // Split into page-safe chunks so PRP1 is sufficient and we never cross a 4KiB page.
    const uint32_t sector_size = 512;
    uint8_t* buf = (uint8_t*)buffer;
    uint16_t remaining = count;

    while (remaining > 0) {
        uint64_t phys = VMemGetPhysAddr((uint64_t)buf);
        uint32_t page_off = (uint32_t)(phys & 0xFFFULL);
        uint32_t bytes_left_in_page = 4096U - page_off;
        uint16_t max_sectors_in_page = (uint16_t)(bytes_left_in_page / sector_size);
        if (max_sectors_in_page == 0) max_sectors_in_page = 1; // Safety: at least one sector

        // NVMe without PRP2/PRP list supports only one page via PRP1 here, cap at 8 sectors (4KiB)
        uint16_t sectors_this = remaining < max_sectors_in_page ? remaining : max_sectors_in_page;
        if (sectors_this > 8) sectors_this = 8;

        NVMeSubmissionEntry cmd = (NVMeSubmissionEntry){0};
        cmd.cdw0 = NVME_CMD_READ | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
        cmd.nsid = 1; // Namespace 1
        cmd.prp1 = phys;
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFULL);
        cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFULL);
        cmd.cdw12 = (uint32_t)(sectors_this - 1); // 0-based count

        int st = NVMe_SubmitIOCommand(&cmd);
        if (st != 0) return st ? st : -1;

        // Advance
        lba += sectors_this;
        buf += (uint32_t)sectors_this * sector_size;
        remaining -= sectors_this;
    }

    return 0;
}

static int NVMe_Flush(void) {
    NVMeSubmissionEntry cmd = (NVMeSubmissionEntry){0};
    cmd.cdw0 = NVME_CMD_FLUSH | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1; // Namespace 1
    return NVMe_SubmitIOCommand(&cmd);
}

int NVMe_WriteSectors(uint64_t lba, uint16_t count, const void* buffer) {
    if (!g_nvme_controller.initialized) return -1;

    const uint32_t sector_size = 512;
    const uint8_t* buf = (const uint8_t*)buffer;
    uint16_t remaining = count;

    while (remaining > 0) {
        uint64_t phys = VMemGetPhysAddr((uint64_t)buf);
        uint32_t page_off = (uint32_t)(phys & 0xFFFULL);
        uint32_t bytes_left_in_page = 4096U - page_off;
        uint16_t max_sectors_in_page = (uint16_t)(bytes_left_in_page / sector_size);
        if (max_sectors_in_page == 0) max_sectors_in_page = 1;

        uint16_t sectors_this = remaining < max_sectors_in_page ? remaining : max_sectors_in_page;
        if (sectors_this > 8) sectors_this = 8;

        NVMeSubmissionEntry cmd = (NVMeSubmissionEntry){0};
        cmd.cdw0 = NVME_CMD_WRITE | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
        cmd.nsid = 1; // Namespace 1
        cmd.prp1 = phys;
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFULL);
        cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFULL);
        cmd.cdw12 = (uint32_t)(sectors_this - 1); // 0-based count

        int st = NVMe_SubmitIOCommand(&cmd);
        if (st != 0) return st ? st : -1;

        lba += sectors_this;
        buf += (uint32_t)sectors_this * sector_size;
        remaining -= sectors_this;
    }

    // Ensure the device flushes its volatile write cache so subsequent reads see consistent data
    int flush_status = NVMe_Flush();
    return flush_status == 0 ? 0 : flush_status;
}

static int NVMe_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer) {
    (void)device;
    return NVMe_ReadSectors(start_lba, count, buffer);
}

static int NVMe_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer) {
    (void)device;
    return NVMe_WriteSectors(start_lba, count, buffer);
}

int NVMe_Init(void) {
    PrintKernel("NVMe: Initializing NVMe driver...\n");
    
    // Find NVMe controller
    PciDevice pci_dev;
    if (PciFindByClass(NVME_CLASS_CODE, NVME_SUBCLASS, NVME_PROG_IF, &pci_dev) != 0) {
        PrintKernel("NVMe: No NVMe controller found\n");
        return -1;
    }
    
    g_nvme_controller.pci_device = pci_dev;
    
    // Enable PCI device
    uint16_t cmd = PciReadConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG);
    cmd |= PCI_CMD_MEM_SPACE_EN | PCI_CMD_BUS_MASTER_EN;
    PciWriteConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG, cmd);
    
    // Map MMIO (NVMe typically uses a 64-bit BAR at BAR0/1)
    uint32_t bar0_val = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_BAR0_REG);
    uint64_t mmio_phys = 0;
    bool is_mem_bar = ((bar0_val & 0x1) == 0); // bit0==0 => memory BAR
    bool is_64bit_bar = ((bar0_val & 0x06) == 0x04);
    if (is_mem_bar) {
        if (is_64bit_bar) {
            uint32_t bar1_val = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_BAR0_REG + 4);
            mmio_phys = ((uint64_t)bar1_val << 32) | (uint64_t)(bar0_val & ~0xFULL);
        } else {
            mmio_phys = (uint64_t)(bar0_val & ~0xFULL);
        }
    }
    if (mmio_phys == 0) {
        PrintKernel("NVMe: Invalid MMIO base address\n");
        return -1;
    }
    
    // Align physical address and size to page boundaries
    uint64_t mmio_phys_aligned = mmio_phys & ~0xFFFULL;
    uint64_t mmio_offset = mmio_phys - mmio_phys_aligned;
    g_nvme_controller.mmio_size = GetPCIMMIOSize(&pci_dev, bar0_val);
    uint64_t mmio_size_aligned = ((g_nvme_controller.mmio_size + mmio_offset + 0xFFFULL) & ~0xFFFULL);
    
    // Allocate virtual space
    volatile uint8_t* mmio_base_raw = (volatile uint8_t*)VMemAlloc(mmio_size_aligned);
    if (!mmio_base_raw) {
        PrintKernel("NVMe: Failed to allocate virtual space\n");
        return -1;
    }
    
    // Unmap RAM pages
    if (VMemUnmap((uint64_t)mmio_base_raw, mmio_size_aligned) != VMEM_SUCCESS) {
        PrintKernel("NVMe: Failed to unmap RAM pages\n");
        VMemFree((void*)mmio_base_raw, mmio_size_aligned);
        return -1;
    }
    
    // Map MMIO
    uint64_t map_flags = PAGE_WRITABLE | PAGE_NOCACHE;
    if (VMemMapMMIO((uint64_t)mmio_base_raw, mmio_phys_aligned, mmio_size_aligned, map_flags) != VMEM_SUCCESS) {
        PrintKernel("NVMe: Failed to map MMIO\n");
        return -1;
    }
    
    // Adjust base pointer to account for offset
    g_nvme_controller.mmio_base = mmio_base_raw + mmio_offset;
    g_nvme_controller.mmio_size = mmio_size_aligned;
    
    __asm__ volatile("mfence" ::: "memory");
    
    // Read capabilities (for doorbell stride)
    uint64_t cap = NVMe_ReadReg64(NVME_CAP);
    g_nvme_controller.dstrd = (uint8_t)((cap >> 32) & 0xF);
    
    // Reset controller
    NVMe_WriteReg32(NVME_CC, 0);
    if (NVMe_WaitReady(0) != 0) {
        PrintKernel("NVMe: Controller reset timeout\n");
        return -1;
    }
    
    // Allocate admin queues
    g_nvme_controller.admin_sq = (NVMeSubmissionEntry*)VMemAlloc(NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    g_nvme_controller.admin_cq = (NVMeCompletionEntry*)VMemAlloc(NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    
    if (!g_nvme_controller.admin_sq || !g_nvme_controller.admin_cq) {
        PrintKernel("NVMe: Failed to allocate admin queues\n");
        return -1;
    }
    
    FastMemset(g_nvme_controller.admin_sq, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    FastMemset(g_nvme_controller.admin_cq, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    
    g_nvme_controller.admin_sq_phys = VMemGetPhysAddr((uint64_t)g_nvme_controller.admin_sq);
    g_nvme_controller.admin_cq_phys = VMemGetPhysAddr((uint64_t)g_nvme_controller.admin_cq);
    g_nvme_controller.admin_sq_tail = 0;
    g_nvme_controller.admin_cq_head = 0;
    g_nvme_controller.admin_cq_phase = 1;
    g_nvme_controller.next_cid = 0;
    
    // Configure admin queues
    NVMe_WriteReg32(NVME_AQA, ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1));
    NVMe_WriteReg64(NVME_ASQ, g_nvme_controller.admin_sq_phys);
    NVMe_WriteReg64(NVME_ACQ, g_nvme_controller.admin_cq_phys);
    
    // Enable controller
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K | NVME_CC_AMS_RR | NVME_CC_SHN_NONE | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;
    NVMe_WriteReg32(NVME_CC, cc);
    
    if (NVMe_WaitReady(1) != 0) {
        PrintKernel("NVMe: Controller enable timeout\n");
        return -1;
    }
    
    // Create I/O queues
    if (NVMe_CreateIOQueues() != 0) {
        PrintKernel("NVMe: Failed to create I/O queues\n");
        return -1;
    }
    
    // Get namespace size
    g_nvme_controller.namespace_size = NVMe_GetNamespaceSize();
    g_nvme_controller.initialized = 1;
    
    // Register block device
    BlockDevice* nvme_device = BlockDeviceRegister(
        DEVICE_TYPE_NVME,
        512, // Block size
        g_nvme_controller.namespace_size,
        "nvme0",
        &g_nvme_controller,
        NVMe_ReadBlocksWrapper,
        NVMe_WriteBlocksWrapper
    );
    
    if (nvme_device) {
        PrintKernel("NVMe: Successfully initialized NVMe controller\n");
        BlockDeviceDetectAndRegisterPartitions(nvme_device);
        return 0;
    }
    
    PrintKernel("NVMe: Failed to register block device\n");
    return -1;
}