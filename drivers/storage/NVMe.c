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
    
    // Update tail pointer
    ctrl->admin_sq_tail = (ctrl->admin_sq_tail + 1) % NVME_ADMIN_QUEUE_SIZE;
    
    // Ring doorbell
    NVMe_WriteReg32(0x1000, ctrl->admin_sq_tail); // Admin SQ doorbell
    
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
            
            // Ring completion doorbell
            NVMe_WriteReg32(0x1004, ctrl->admin_cq_head); // Admin CQ doorbell
            
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
    
    // Update tail pointer
    ctrl->io_sq_tail = (ctrl->io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
    
    // Ring doorbell (I/O queue 1)
    NVMe_WriteReg32(0x1008, ctrl->io_sq_tail); // I/O SQ doorbell
    
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
            
            // Ring completion doorbell
            NVMe_WriteReg32(0x100C, ctrl->io_cq_head); // I/O CQ doorbell
            
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
    cmd.cdw0 = (++ctrl->next_cid) | (0x05 << 8); // CREATE_IO_CQ opcode
    cmd.prp1 = ctrl->io_cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1; // Queue size and ID
    cmd.cdw11 = 1; // Physically contiguous
    
    if (NVMe_SubmitAdminCommand(&cmd) != 0) {
        PrintKernel("NVMe: Failed to create I/O completion queue\n");
        return -1;
    }
    
    // Create I/O Submission Queue
    FastMemset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = (++ctrl->next_cid) | (0x01 << 8); // CREATE_IO_SQ opcode
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
    cmd.cdw0 = (++g_nvme_controller.next_cid) | (NVME_ADMIN_IDENTIFY << 8);
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
    
    uint64_t buffer_phys = VMemGetPhysAddr((uint64_t)buffer);
    
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = (++g_nvme_controller.next_cid) | (NVME_CMD_READ << 8);
    cmd.nsid = 1; // Namespace 1
    cmd.prp1 = buffer_phys;
    cmd.cdw10 = lba & 0xFFFFFFFF;
    cmd.cdw11 = (lba >> 32) & 0xFFFFFFFF;
    cmd.cdw12 = (count - 1); // 0-based count
    
    return NVMe_SubmitIOCommand(&cmd);
}

int NVMe_WriteSectors(uint64_t lba, uint16_t count, const void* buffer) {
    if (!g_nvme_controller.initialized) return -1;
    
    uint64_t buffer_phys = VMemGetPhysAddr((uint64_t)buffer);
    
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = (++g_nvme_controller.next_cid) | (NVME_CMD_WRITE << 8);
    cmd.nsid = 1; // Namespace 1
    cmd.prp1 = buffer_phys;
    cmd.cdw10 = lba & 0xFFFFFFFF;
    cmd.cdw11 = (lba >> 32) & 0xFFFFFFFF;
    cmd.cdw12 = (count - 1); // 0-based count
    
    return NVMe_SubmitIOCommand(&cmd);
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
    
    // Map MMIO (following AHCI pattern)
    uint64_t mmio_phys = pci_dev.bar0 & ~0xF;
    if (mmio_phys == 0) {
        PrintKernel("NVMe: Invalid MMIO base address\n");
        return -1;
    }
    
    // Align physical address and size to page boundaries
    uint64_t mmio_phys_aligned = mmio_phys & ~0xFFF;
    uint64_t mmio_offset = mmio_phys - mmio_phys_aligned;
    g_nvme_controller.mmio_size = GetPCIMMIOSize(&pci_dev, pci_dev.bar0);
    uint64_t mmio_size_aligned = ((g_nvme_controller.mmio_size + mmio_offset + 0xFFF) & ~0xFFF);
    
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