#ifndef VOIDFRAME_NVME_H
#define VOIDFRAME_NVME_H

#include <stdint.h>
#include <PCI/PCI.h>
#include <kernel/atomic/SpinlockRust.h>

// NVMe PCI Class/Subclass
#define NVME_CLASS_CODE     0x01
#define NVME_SUBCLASS       0x08
#define NVME_PROG_IF        0x02

// NVMe Controller Registers (BAR0 offsets)
#define NVME_CAP            0x00    // Controller Capabilities
#define NVME_VS             0x08    // Version
#define NVME_CC             0x14    // Controller Configuration
#define NVME_CSTS           0x1C    // Controller Status
#define NVME_AQA            0x24    // Admin Queue Attributes
#define NVME_ASQ            0x28    // Admin Submission Queue Base
#define NVME_ACQ            0x30    // Admin Completion Queue Base

// Controller Configuration bits
#define NVME_CC_EN          (1 << 0)    // Enable
#define NVME_CC_CSS_NVM     (0 << 4)    // NVM Command Set
#define NVME_CC_MPS_4K      (0 << 7)    // 4KB Memory Page Size
#define NVME_CC_AMS_RR      (0 << 11)   // Round Robin
#define NVME_CC_SHN_NONE    (0 << 14)   // No shutdown
#define NVME_CC_IOSQES_64   (6 << 16)   // 64-byte SQ entries
#define NVME_CC_IOCQES_16   (4 << 20)   // 16-byte CQ entries

// Controller Status bits
#define NVME_CSTS_RDY       (1 << 0)    // Ready
#define NVME_CSTS_CFS       (1 << 1)    // Controller Fatal Status
#define NVME_CSTS_SHST_MASK (3 << 2)    // Shutdown Status

// NVMe Commands
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_CMD_READ           0x02
#define NVME_CMD_WRITE          0x01
#define NVME_CMD_FLUSH          0x00

// Queue sizes
#define NVME_ADMIN_QUEUE_SIZE   64
#define NVME_IO_QUEUE_SIZE      256

#define PRP_LIST_ENTRIES 512

// NVMe Submission Queue Entry
typedef struct {
    uint32_t cdw0;      // Command Dword 0
    uint32_t nsid;      // Namespace ID
    uint64_t rsvd2;
    uint64_t mptr;      // Metadata Pointer
    uint64_t prp1;      // PRP Entry 1
    uint64_t prp2;      // PRP Entry 2
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) NVMeSubmissionEntry;

// NVMe Completion Queue Entry
typedef struct {
    uint32_t dw0;       // Command-specific
    uint32_t dw1;       // Reserved
    uint16_t sq_head;   // Submission Queue Head
    uint16_t sq_id;     // Submission Queue ID
    uint16_t cid;       // Command ID
    uint16_t status;    // Status Field
} __attribute__((packed)) NVMeCompletionEntry;

// NVMe Controller structure
typedef struct {
    PciDevice pci_device;
    volatile uint8_t* mmio_base;
    uint64_t mmio_size;
    RustSpinLock* lock;
    uint64_t* prp_list;
    uint64_t prp_list_phys;
    
    // Doorbell parameters
    uint8_t dstrd; // CAP.DSTRD value (stride as 2^n of 4-byte units)
    
    // Admin queues
    NVMeSubmissionEntry* admin_sq;
    NVMeCompletionEntry* admin_cq;
    uint64_t admin_sq_phys;
    uint64_t admin_cq_phys;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint8_t admin_cq_phase;
    
    // I/O queues
    NVMeSubmissionEntry* io_sq;
    NVMeCompletionEntry* io_cq;
    uint64_t io_sq_phys;
    uint64_t io_cq_phys;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint8_t io_cq_phase;
    
    uint16_t next_cid;
    uint32_t namespace_size;
    int initialized;
} NVMeController;

// Function prototypes
int NVMe_Init(void);
void NVMe_Shutdown(void);
int NVMe_ReadSectors(uint64_t lba, uint16_t count, void* buffer);
int NVMe_WriteSectors(uint64_t lba, uint16_t count, const void* buffer);

#endif // VOIDFRAME_NVME_H