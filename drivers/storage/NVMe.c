#include "NVMe.h"
#include "Console.h"
#include "TSC.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VMem.h"
#include "BlockDevice.h"
#include "DriveNaming.h"
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
    uint64_t timeout_ms = 5000;
    uint64_t start_time = GetTimeInMs();

    while ((GetTimeInMs() - start_time) < timeout_ms) {
        uint32_t csts = NVMe_ReadReg32(NVME_CSTS);
        if (ready && (csts & NVME_CSTS_RDY)) return 0;
        if (!ready && !(csts & NVME_CSTS_RDY)) return 0;
        delay_us(1000);
    }
    return -1;
}

static int NVMe_SubmitAdminCommand(NVMeSubmissionEntry* cmd) {
    NVMeController* ctrl = &g_nvme_controller;
    
    uint64_t irq_flags = rust_spinlock_lock_irqsave(ctrl->lock);
    
    FastMemcpy(&ctrl->admin_sq[ctrl->admin_sq_tail], cmd, sizeof(NVMeSubmissionEntry));
    __asm__ volatile("mfence" ::: "memory");
    
    ctrl->admin_sq_tail = (ctrl->admin_sq_tail + 1) % NVME_ADMIN_QUEUE_SIZE;
    
    uint32_t db_shift = 2 + ctrl->dstrd;
    uint32_t asq_db_off = 0x1000 + ((0 * 2 + 0) << db_shift);
    NVMe_WriteReg32(asq_db_off, ctrl->admin_sq_tail);
    
    rust_spinlock_unlock_irqrestore(ctrl->lock, irq_flags);

    uint64_t timeout_ms = 5000;
    uint64_t start_time = GetTimeInMs();

    while ((GetTimeInMs() - start_time) < timeout_ms) {
        NVMeCompletionEntry* cqe = &ctrl->admin_cq[ctrl->admin_cq_head];
        uint16_t status = cqe->status;
        
        if ((status & 1) == ctrl->admin_cq_phase) {
            ctrl->admin_cq_head = (ctrl->admin_cq_head + 1) % NVME_ADMIN_QUEUE_SIZE;
            if (ctrl->admin_cq_head == 0) {
                ctrl->admin_cq_phase = !ctrl->admin_cq_phase;
            }
            
            uint32_t acq_db_off = 0x1000 + ((0 * 2 + 1) << db_shift);
            NVMe_WriteReg32(acq_db_off, ctrl->admin_cq_head);
            
            return (status >> 1) & 0x7FF;
        }
        delay_us(100);
    }
    
    return -1;
}

static int NVMe_SubmitIOCommand(NVMeSubmissionEntry* cmd) {
    NVMeController* ctrl = &g_nvme_controller;
    
    uint64_t irq_flags = rust_spinlock_lock_irqsave(ctrl->lock);
    
    FastMemcpy(&ctrl->io_sq[ctrl->io_sq_tail], cmd, sizeof(NVMeSubmissionEntry));
    __asm__ volatile("mfence" ::: "memory");
    
    ctrl->io_sq_tail = (ctrl->io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
    
    uint32_t db_shift = 2 + ctrl->dstrd;
    uint32_t iosq_db_off = 0x1000 + ((1 * 2 + 0) << db_shift);
    NVMe_WriteReg32(iosq_db_off, ctrl->io_sq_tail);
    
    rust_spinlock_unlock_irqrestore(ctrl->lock, irq_flags);

    uint64_t timeout_ms = 5000;
    uint64_t start_time = GetTimeInMs();

    while ((GetTimeInMs() - start_time) < timeout_ms) {
        NVMeCompletionEntry* cqe = &ctrl->io_cq[ctrl->io_cq_head];
        uint16_t status = cqe->status;
        
        if ((status & 1) == ctrl->io_cq_phase) {
            ctrl->io_cq_head = (ctrl->io_cq_head + 1) % NVME_IO_QUEUE_SIZE;
            if (ctrl->io_cq_head == 0) {
                ctrl->io_cq_phase = !ctrl->io_cq_phase;
            }
            
            uint32_t iocq_db_off = 0x1000 + ((1 * 2 + 1) << db_shift);
            NVMe_WriteReg32(iocq_db_off, ctrl->io_cq_head);
            
            return (status >> 1) & 0x7FF;
        }
        delay_us(50);
    }
    
    return -1;
}

static int NVMe_CreateIOQueues(void) {
    NVMeController* ctrl = &g_nvme_controller;
    
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
    
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = 0x05 | ((uint32_t)(++ctrl->next_cid) << 16);
    cmd.prp1 = ctrl->io_cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = 1;
    
    if (NVMe_SubmitAdminCommand(&cmd) != 0) {
        PrintKernel("NVMe: Failed to create I/O completion queue\n");
        return -1;
    }
    
    FastMemset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = 0x01 | ((uint32_t)(++ctrl->next_cid) << 16);
    cmd.prp1 = ctrl->io_sq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = (1 << 16) | 1;
    
    if (NVMe_SubmitAdminCommand(&cmd) != 0) {
        PrintKernel("NVMe: Failed to create I/O submission queue\n");
        return -1;
    }
    
    return 0;
}

static uint64_t NVMe_GetNamespaceSize(void) {
    uint8_t* identify_data = (uint8_t*)KernelMemoryAlloc(4096);
    if (!identify_data) return 0;
    
    FastMemset(identify_data, 0, 4096);
    uint64_t identify_phys = VMemGetPhysAddr((uint64_t)identify_data);
    
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = NVME_ADMIN_IDENTIFY | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1;
    cmd.prp1 = identify_phys;
    cmd.cdw10 = 0;
    
    int result = NVMe_SubmitAdminCommand(&cmd);
    if (result != 0) {
        KernelFree(identify_data);
        return 0;
    }
    
    uint64_t nsze = *(uint64_t*)identify_data;
    KernelFree(identify_data);
    return nsze;
}

static int NVMe_SetupPrpList(uint64_t buffer_phys, uint32_t total_bytes) {
    NVMeController* ctrl = &g_nvme_controller;
    uint32_t page_size = 4096;
    uint32_t offset_in_first_page = buffer_phys & (page_size - 1);
    uint32_t bytes_in_first_page = page_size - offset_in_first_page;

    int num_prp_entries = 0;
    if (total_bytes <= bytes_in_first_page) {
        return 0; // No PRP list needed
    } else {
        uint32_t bytes_after_first_page = total_bytes - bytes_in_first_page;
        num_prp_entries = (bytes_after_first_page + page_size - 1) / page_size;
    }

    if (num_prp_entries > PRP_LIST_ENTRIES) {
        return -1; // Too large for our PRP list
    }

    uint64_t current_phys = buffer_phys + bytes_in_first_page;
    for (int i = 0; i < num_prp_entries; ++i) {
        ctrl->prp_list[i] = current_phys;
        current_phys += page_size;
    }

    return num_prp_entries;
}

int NVMe_ReadSectors(uint64_t lba, uint16_t count, void* buffer) {
    if (!g_nvme_controller.initialized) return -1;

    uint32_t total_bytes = count * 512;
    uint64_t buffer_phys = VMemGetPhysAddr((uint64_t)buffer);

    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = NVME_CMD_READ | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFULL);
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFULL);
    cmd.cdw12 = (uint32_t)(count - 1);

    int num_prp_entries = NVMe_SetupPrpList(buffer_phys, total_bytes);
    if (num_prp_entries < 0) {
        return -1; // Error
    }

    cmd.prp1 = buffer_phys;
    if (num_prp_entries > 0) {
        cmd.prp2 = g_nvme_controller.prp_list_phys;
    }

    return NVMe_SubmitIOCommand(&cmd);
}

static int NVMe_Flush(void) {
    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = NVME_CMD_FLUSH | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1;
    return NVMe_SubmitIOCommand(&cmd);
}

int NVMe_WriteSectors(uint64_t lba, uint16_t count, const void* buffer) {
    if (!g_nvme_controller.initialized) return -1;

    uint32_t total_bytes = count * 512;
    uint64_t buffer_phys = VMemGetPhysAddr((uint64_t)buffer);

    NVMeSubmissionEntry cmd = {0};
    cmd.cdw0 = NVME_CMD_WRITE | ((uint32_t)(++g_nvme_controller.next_cid) << 16);
    cmd.nsid = 1;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFULL);
    cmd.cdw11 = (uint32_t)((lba >> 32) & 0xFFFFFFFFULL);
    cmd.cdw12 = (uint32_t)(count - 1);

    int num_prp_entries = NVMe_SetupPrpList(buffer_phys, total_bytes);
    if (num_prp_entries < 0) {
        return -1; // Error
    }

    cmd.prp1 = buffer_phys;
    if (num_prp_entries > 0) {
        cmd.prp2 = g_nvme_controller.prp_list_phys;
    }

    int st = NVMe_SubmitIOCommand(&cmd);
    if (st != 0) return st;

    return NVMe_Flush();
}

void NVMe_Shutdown(void) {
    if (!g_nvme_controller.initialized) return;

    PrintKernel("NVMe: Shutting down NVMe controller...\n");

    NVMe_WriteReg32(NVME_CC, 0);
    NVMe_WaitReady(0);

    if (g_nvme_controller.admin_sq) VMemFree(g_nvme_controller.admin_sq, NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    if (g_nvme_controller.admin_cq) VMemFree(g_nvme_controller.admin_cq, NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    if (g_nvme_controller.io_sq) VMemFree(g_nvme_controller.io_sq, NVME_IO_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    if (g_nvme_controller.io_cq) VMemFree(g_nvme_controller.io_cq, NVME_IO_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    if (g_nvme_controller.prp_list) VMemFree(g_nvme_controller.prp_list, PRP_LIST_ENTRIES * sizeof(uint64_t));

    if (g_nvme_controller.mmio_base) {
        VMemUnmap((uint64_t)g_nvme_controller.mmio_base, g_nvme_controller.mmio_size);
        VMemFree((void*)g_nvme_controller.mmio_base, g_nvme_controller.mmio_size);
    }

    if (g_nvme_controller.lock) {
        rust_spinlock_free(g_nvme_controller.lock);
    }

    FastMemset(&g_nvme_controller, 0, sizeof(NVMeController));
    PrintKernel("NVMe: Shutdown complete.\n");
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
    
    PciDevice pci_dev;
    if (PciFindByClass(NVME_CLASS_CODE, NVME_SUBCLASS, NVME_PROG_IF, &pci_dev) != 0) {
        PrintKernel("NVMe: No NVMe controller found\n");
        return -1;
    }
    
    g_nvme_controller.pci_device = pci_dev;
    g_nvme_controller.lock = rust_spinlock_new();

    g_nvme_controller.prp_list = (uint64_t*)VMemAlloc(PRP_LIST_ENTRIES * sizeof(uint64_t));
    if (!g_nvme_controller.prp_list) {
        PrintKernel("NVMe: Failed to allocate PRP list\n");
        NVMe_Shutdown();
        return -1;
    }
    g_nvme_controller.prp_list_phys = VMemGetPhysAddr((uint64_t)g_nvme_controller.prp_list);

    uint16_t cmd = PciReadConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG);
    cmd |= PCI_CMD_MEM_SPACE_EN | PCI_CMD_BUS_MASTER_EN;
    PciWriteConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG, cmd);
    
    uint32_t bar0_val = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_BAR0_REG);
    uint64_t mmio_phys = 0;
    if ((bar0_val & 0x1) == 0) {
        if ((bar0_val & 0x06) == 0x04) {
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
    
    uint64_t mmio_phys_aligned = mmio_phys & ~0xFFFULL;
    uint64_t mmio_offset = mmio_phys - mmio_phys_aligned;
    g_nvme_controller.mmio_size = GetPCIMMIOSize(&pci_dev, bar0_val);
    uint64_t mmio_size_aligned = ((g_nvme_controller.mmio_size + mmio_offset + 0xFFFULL) & ~0xFFFULL);
    
    volatile uint8_t* mmio_base_raw = (volatile uint8_t*)VMemAlloc(mmio_size_aligned);
    if (!mmio_base_raw) {
        PrintKernel("NVMe: Failed to allocate virtual space\n");
        return -1;
    }
    
    if (VMemUnmap((uint64_t)mmio_base_raw, mmio_size_aligned) != VMEM_SUCCESS) {
        PrintKernel("NVMe: Failed to unmap RAM pages\n");
        VMemFree((void*)mmio_base_raw, mmio_size_aligned);
        return -1;
    }
    
    uint64_t map_flags = PAGE_WRITABLE | PAGE_NOCACHE;
    if (VMemMapMMIO((uint64_t)mmio_base_raw, mmio_phys_aligned, mmio_size_aligned, map_flags) != VMEM_SUCCESS) {
        PrintKernel("NVMe: Failed to map MMIO\n");
        return -1;
    }
    
    g_nvme_controller.mmio_base = mmio_base_raw + mmio_offset;
    g_nvme_controller.mmio_size = mmio_size_aligned;
    
    __asm__ volatile("mfence" ::: "memory");
    
    uint64_t cap = NVMe_ReadReg64(NVME_CAP);
    g_nvme_controller.dstrd = (uint8_t)((cap >> 32) & 0xF);
    
    NVMe_WriteReg32(NVME_CC, 0);
    if (NVMe_WaitReady(0) != 0) {
        PrintKernel("NVMe: Controller reset timeout\n");
        return -1;
    }
    
    g_nvme_controller.admin_sq = (NVMeSubmissionEntry*)VMemAlloc(NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeSubmissionEntry));
    g_nvme_controller.admin_cq = (NVMeCompletionEntry*)VMemAlloc(NVME_ADMIN_QUEUE_SIZE * sizeof(NVMeCompletionEntry));
    
    if (!g_nvme_controller.admin_sq || !g_nvme_controller.admin_cq) {
        PrintKernel("NVMe: Failed to allocate admin queues\n");
        NVMe_Shutdown();
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
    
    NVMe_WriteReg32(NVME_AQA, ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1));
    NVMe_WriteReg64(NVME_ASQ, g_nvme_controller.admin_sq_phys);
    NVMe_WriteReg64(NVME_ACQ, g_nvme_controller.admin_cq_phys);
    
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K | NVME_CC_AMS_RR | NVME_CC_SHN_NONE | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;
    NVMe_WriteReg32(NVME_CC, cc);
    
    if (NVMe_WaitReady(1) != 0) {
        PrintKernel("NVMe: Controller enable timeout\n");
        NVMe_Shutdown();
        return -1;
    }
    
    if (NVMe_CreateIOQueues() != 0) {
        PrintKernel("NVMe: Failed to create I/O queues\n");
        NVMe_Shutdown();
        return -1;
    }
    
    g_nvme_controller.namespace_size = NVMe_GetNamespaceSize();
    if (g_nvme_controller.namespace_size == 0) {
        PrintKernel("NVMe: Failed to get namespace size\n");
        NVMe_Shutdown();
        return -1;
    }

    g_nvme_controller.initialized = 1;
    
    char dev_name[16];
    GenerateDriveNameInto(DEVICE_TYPE_NVME, dev_name);

    BlockDevice* nvme_device = BlockDeviceRegister(
        DEVICE_TYPE_NVME,
        512,
        g_nvme_controller.namespace_size,
        dev_name,
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
    NVMe_Shutdown();
    return -1;
}