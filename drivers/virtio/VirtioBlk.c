#include <VirtioBlk.h>
#include <Atomics.h>
#include <BlockDevice.h>
#include <Console.h>
#include <DriveNaming.h>
#include <Format.h>
#include <PCI/PCI.h>
#include <Spinlock.h>
#include <SpinlockRust.h>
#include <VMem.h>
#include <Virtio.h>
#include <stdbool.h>
// Globals to hold the capability structures we find
static RustSpinLock* virtio_lock = NULL;
static struct VirtioPciCap cap_common_cfg;
static struct VirtioPciCap cap_notify_cfg;
static struct VirtioPciCap cap_isr_cfg;
static struct VirtioPciCap cap_device_cfg;
static bool have_common_cfg = false;
static bool have_notify_cfg = false;
volatile uint32_t* notify_ptr = NULL;
// --- Virtqueue state ---
static struct VirtqDesc* vq_desc_table;
static struct VirtqAvail* vq_avail_ring;
static struct VirtqUsed* vq_used_ring;
static uint16_t vq_size;
static uint16_t vq_next_desc_idx = 0;
static uint16_t last_used_idx = 0;

// A structure to keep track of pending requests
struct VirtioBlkRequest {
    struct VirtioBlkReq* req_hdr;
    uint8_t* status;
};

// Let's assume a maximum of 128 pending requests
#define MAX_PENDING_REQS 128
static struct VirtioBlkRequest pending_reqs[MAX_PENDING_REQS];

volatile struct VirtioPciCommonCfg* common_cfg_ptr;

// Forward declarations
static int VirtioBlk_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer);
static int VirtioBlk_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer);

void ReadVirtioCapability(PciDevice device, uint8_t cap_offset, struct VirtioPciCap* cap) {
    // cap->cap_vndr is known to be 0x09, not reading
    cap->cap_next = PciConfigReadByte(device.bus, device.device, device.function, cap_offset + 1);
    cap->cap_len  = PciConfigReadByte(device.bus, device.device, device.function, cap_offset + 2);
    cap->cfg_type = PciConfigReadByte(device.bus, device.device, device.function, cap_offset + 3);
    cap->bar      = PciConfigReadByte(device.bus, device.device, device.function, cap_offset + 4);
    // cap->padding is bytes 5, 6, 7
    cap->offset   = PciConfigReadDWord(device.bus, device.device, device.function, cap_offset + 8);
    cap->length   = PciConfigReadDWord(device.bus, device.device, device.function, cap_offset + 12);
}

static int VirtioBlk_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer) {
    (void)device;
    return VirtioBlkRead(start_lba, buffer, count);
}

static int VirtioBlk_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer) {
    (void)device;
    return VirtioBlkWrite(start_lba, (void*)buffer, count);
}

// Implementation for the VirtIO Block device driver.

void InitializeVirtioBlk(PciDevice device) {
    virtio_lock = rust_spinlock_new();
    if (!virtio_lock) {
        PrintKernelError("VirtIO-Blk: - Failed to initialize spinlock.\n");
        return;
    }
    PrintKernel("VirtIO-Blk: Initializing device at B/D/F ");
    PrintKernelHex(device.bus); PrintKernel("/"); PrintKernelHex(device.device); PrintKernel("/"); PrintKernelHex(device.function);
    PrintKernel("\n");

    // Check for capabilities list support
    uint16_t status_reg = (PciConfigReadDWord(device.bus, device.device, device.function, 0x04) >> 16);
    if (!(status_reg & (1 << 4))) {
        PrintKernel("VirtIO-Blk: Error - Device does not support capabilities list.\n");
        return;
    }

    // Loop through capabilities
    uint8_t cap_pointer = PciConfigReadByte(device.bus, device.device, device.function, 0x34);
    while (cap_pointer != 0) {
        uint8_t cap_id = PciConfigReadByte(device.bus, device.device, device.function, cap_pointer);

        if (cap_id == 0x09) { // PCI_CAP_ID_VNDR
            struct VirtioPciCap temp_cap;
            ReadVirtioCapability(device, cap_pointer, &temp_cap);

            switch (temp_cap.cfg_type) {
                case VIRTIO_CAP_COMMON_CFG:
                    cap_common_cfg = temp_cap;
                    have_common_cfg = true;
                    break;
                case VIRTIO_CAP_NOTIFY_CFG:
                    cap_notify_cfg = temp_cap;
                    have_notify_cfg = true;
                    break;
                case VIRTIO_CAP_ISR_CFG:
                    cap_isr_cfg = temp_cap;
                    break;
                case VIRTIO_CAP_DEVICE_CFG:
                    cap_device_cfg = temp_cap;
                    break;
                case VIRTIO_CAP_PCI_CFG:
                    break;
            }
        }
        cap_pointer = PciConfigReadByte(device.bus, device.device, device.function, cap_pointer + 1);
    }

    if (!have_common_cfg) {
        PrintKernel("VirtIO-Blk: Error - Did not find VIRTIO_CAP_COMMON_CFG.\n");
        return;
    }

    // Enable Bus Mastering and memory space on the device
    uint16_t command_reg = PciConfigReadDWord(device.bus, device.device, device.function, PCI_COMMAND_REG);
    command_reg |= (PCI_CMD_MEM_SPACE_EN | PCI_CMD_BUS_MASTER_EN);
    PciConfigWriteDWord(device.bus, device.device, device.function, PCI_COMMAND_REG, command_reg);

    // Get the physical address from the BAR
    uint32_t bar_raw = PciConfigReadDWord(device.bus, device.device, device.function, 0x10 + (cap_common_cfg.bar * 4));
    uint64_t phys_addr = bar_raw & 0xFFFFFFF0;

    // Allocate virtual address space for the common config region
    void* virt_addr = VMemAlloc(cap_common_cfg.length);
    if (!virt_addr) {
        PrintKernel("VirtIO-Blk: Error - VMemAlloc failed.\n");
        return;
    }

    // Unmap the RAM pages VMemAlloc mapped by default
    if (VMemUnmap((uint64_t)virt_addr, cap_common_cfg.length) != VMEM_SUCCESS) {
        PrintKernel("VirtIO-Blk: Error - VMemUnmap failed.\n");
        VMemFree(virt_addr, cap_common_cfg.length);
        return;
    }

    // Map the physical memory into our allocated virtual space
    if (VMemMapMMIO((uint64_t)virt_addr, phys_addr, cap_common_cfg.length, VMEM_WRITE | VMEM_NOCACHE) != VMEM_SUCCESS) {
        PrintKernel("VirtIO-Blk: Error - VMemMapMMIO failed.\n");
        // Don't VMemFree here as the pages are already unmapped
        return;
    }
    common_cfg_ptr = (struct VirtioPciCommonCfg*)((uint64_t)virt_addr + cap_common_cfg.offset);

    // Map the Notification BAR and get the multiplier
    uint32_t notify_off_multiplier = 0;
    if (have_notify_cfg) {
        uint32_t notify_bar_raw = PciConfigReadDWord(device.bus, device.device, device.function, 0x10 + (cap_notify_cfg.bar * 4));
        uint64_t notify_phys_addr = notify_bar_raw & 0xFFFFFFF0;
        void* notify_virt_addr = VMemAlloc(cap_notify_cfg.length);
        VMemUnmap((uint64_t)notify_virt_addr, cap_notify_cfg.length);
        VMemMapMMIO((uint64_t)notify_virt_addr, notify_phys_addr, cap_notify_cfg.length, VMEM_WRITE | VMEM_NOCACHE);
        notify_ptr = (uint32_t*)((uint64_t)notify_virt_addr + cap_notify_cfg.offset);
        
        // The multiplier is at a different offset within the same BAR, described by another capability.
        // For now, we will assume a multiplier of 1, which is common.
        // A full implementation would parse the notification capability structure itself.
        notify_off_multiplier = 1; 
    }

    // --- Begin Device Initialization ---
    PrintKernel("VirtIO-Blk: Starting device initialization...\n");
    
    // 1. Reset device
    common_cfg_ptr->device_status = 0;
    
    // Small delay after reset
    for (volatile int i = 0; i < 1000; i++) {}
    
    // 2. Set ACKNOWLEDGE bit
    common_cfg_ptr->device_status |= (1 << 0);
    PrintKernel("VirtIO-Blk: ACKNOWLEDGE set\n");
    
    // 3. Set DRIVER bit
    common_cfg_ptr->device_status |= (1 << 1);
    PrintKernel("VirtIO-Blk: DRIVER set\n");

    // 4. Feature Negotiation
    common_cfg_ptr->driver_feature_select = 0;
    uint32_t device_features = common_cfg_ptr->device_feature;
    PrintKernel("VirtIO-Blk: Device features: 0x");
    PrintKernelHex(device_features);
    PrintKernel("\n");
    
    common_cfg_ptr->driver_feature_select = 0;
    common_cfg_ptr->driver_feature = 0;
    PrintKernel("VirtIO-Blk: Features negotiated\n");

    // 5. Set FEATURES_OK status bit
    common_cfg_ptr->device_status |= (1 << 3);
    PrintKernel("VirtIO-Blk: FEATURES_OK set\n");

    // 6. Re-read status to ensure device accepted features
    uint8_t status = common_cfg_ptr->device_status;
    PrintKernel("VirtIO-Blk: Device status: 0x");
    PrintKernelHex(status);
    PrintKernel("\n");
    
    if (!(status & (1 << 3))) {
        PrintKernel("VirtIO-Blk: Error - Device rejected features!\n");
        return;
    }

    // --- Step 7: Virtqueue Setup ---
    common_cfg_ptr->queue_select = 0;
    
    // Reset queue first
    common_cfg_ptr->queue_enable = 0;
    
    vq_size = common_cfg_ptr->queue_size;
    if (vq_size == 0) {
        PrintKernel("VirtIO-Blk: Error - Queue 0 is not available.\n");
        return;
    }
    
    PrintKernel("VirtIO-Blk: Queue size: ");
    PrintKernelInt(vq_size);
    PrintKernel("\n");

    // Allocate memory for the virtqueue components
    vq_desc_table = VMemAlloc(sizeof(struct VirtqDesc) * vq_size);
    vq_avail_ring = VMemAlloc(sizeof(struct VirtqAvail) + sizeof(uint16_t) * vq_size);
    vq_used_ring = VMemAlloc(sizeof(struct VirtqUsed) + sizeof(struct VirtqUsedElem) * vq_size);

    if (!vq_desc_table || !vq_avail_ring || !vq_used_ring) {
        PrintKernel("VirtIO-Blk: Error - Failed to allocate memory for virtqueue.\n");
        if (vq_desc_table)
            VMemFree(vq_desc_table, sizeof(struct VirtqDesc) * vq_size);
        if (vq_avail_ring)
            VMemFree(vq_avail_ring,
                     sizeof(struct VirtqAvail) + sizeof(uint16_t) * vq_size);
        if (vq_used_ring)
            VMemFree(vq_used_ring,
                     sizeof(struct VirtqUsed) + sizeof(struct VirtqUsedElem) * vq_size);
        return;
    }

    // Tell the device the physical addresses of our structures
    common_cfg_ptr->queue_desc = VMemGetPhysAddr((uint64_t)vq_desc_table);
    common_cfg_ptr->queue_driver = VMemGetPhysAddr((uint64_t)vq_avail_ring);
    common_cfg_ptr->queue_device = VMemGetPhysAddr((uint64_t)vq_used_ring);

    // Enable the queue
    common_cfg_ptr->queue_enable = 1;

    // Initialize the pending requests array
    for (int i = 0; i < MAX_PENDING_REQS; ++i) {
        pending_reqs[i].req_hdr = NULL;
        pending_reqs[i].status = NULL;
    }

    // 8. Set DRIVER_OK status bit
    common_cfg_ptr->device_status |= (1 << 2);
    
    PrintKernelSuccess("VirtIO-Blk: Device initialized successfully\n");
    
    // Register as block device
    uint64_t total_sectors = 0x1000000; // Default 8GB, should read from device config
    char dev_name[16];
    GenerateDriveNameInto(DEVICE_TYPE_VIRTIO, dev_name);
    BlockDevice* dev = BlockDeviceRegister(
        DEVICE_TYPE_VIRTIO,
        512,
        total_sectors,
        dev_name,
        (void*)(uintptr_t)1, // Device instance
        VirtioBlk_ReadBlocksWrapper,
        VirtioBlk_WriteBlocksWrapper
    );
    
    if (dev) {
        PrintKernel("VirtIO-Blk: Registered block device: ");
        PrintKernel(dev_name);
        PrintKernel("\n");
        BlockDeviceDetectAndRegisterPartitions(dev);
    } else {
        PrintKernel("VirtIO-Blk: Failed to register block device\n");
    }
}

int VirtioBlkRead(uint64_t sector, void* buffer, uint32_t count) {
    if (!virtio_lock || !common_cfg_ptr) return -1;
    
    rust_spinlock_lock(virtio_lock);
    
    // Allocate request header and status
    struct VirtioBlkReq* req = VMemAlloc(sizeof(struct VirtioBlkReq));
    uint8_t* status = VMemAlloc(1);
    
    if (!req || !status) {
        if (req) VMemFree(req, sizeof(struct VirtioBlkReq));
        if (status) VMemFree(status, 1);
        rust_spinlock_unlock(virtio_lock);
        return -1;
    }
    
    req->type = VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;
    *status = 0xFF;
    
    // Set up descriptor chain
    uint16_t desc_idx = vq_next_desc_idx;
    
    // Descriptor 0: Request header (device reads)
    vq_desc_table[desc_idx].addr = VMemGetPhysAddr((uint64_t)req);
    vq_desc_table[desc_idx].len = sizeof(struct VirtioBlkReq);
    vq_desc_table[desc_idx].flags = VIRTQ_DESC_F_NEXT;
    vq_desc_table[desc_idx].next = (desc_idx + 1) % vq_size;
    
    // Descriptor 1: Data buffer (device writes)
    vq_desc_table[(desc_idx + 1) % vq_size].addr = VMemGetPhysAddr((uint64_t)buffer);
    vq_desc_table[(desc_idx + 1) % vq_size].len = count * 512;
    vq_desc_table[(desc_idx + 1) % vq_size].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    vq_desc_table[(desc_idx + 1) % vq_size].next = (desc_idx + 2) % vq_size;
    
    // Descriptor 2: Status byte (device writes)
    vq_desc_table[(desc_idx + 2) % vq_size].addr = VMemGetPhysAddr((uint64_t)status);
    vq_desc_table[(desc_idx + 2) % vq_size].len = 1;
    vq_desc_table[(desc_idx + 2) % vq_size].flags = VIRTQ_DESC_F_WRITE;
    vq_desc_table[(desc_idx + 2) % vq_size].next = 0;
    
    // Add to available ring
    uint16_t avail_idx = vq_avail_ring->idx % vq_size;
    vq_avail_ring->ring[avail_idx] = desc_idx;
    vq_avail_ring->idx++;

    // Notify device
    __asm__ volatile("" ::: "memory");
    if (notify_ptr) { *notify_ptr = 0; }

    // Wait for completion (simple polling)
    uint64_t spins = 0, max_spins = 10000000;
    while (vq_used_ring->idx == last_used_idx && spins++ < max_spins) {
        __asm__ volatile("pause");
    }
    if (vq_used_ring->idx == last_used_idx) {
        VMemFree(req, sizeof(struct VirtioBlkReq));
        VMemFree(status, 1);
        rust_spinlock_unlock(virtio_lock);
        return -1;
    }

    last_used_idx = vq_used_ring->idx;
    vq_next_desc_idx = (desc_idx + 3) % vq_size;

    int result = (*status == 0) ? 0 : -1;
    
    VMemFree(req, sizeof(struct VirtioBlkReq));
    VMemFree(status, 1);
    rust_spinlock_unlock(virtio_lock);
    
    return result;
}

int VirtioBlkWrite(uint64_t sector, void* buffer, uint32_t count) {
    if (!virtio_lock || !common_cfg_ptr) return -1;
    
    rust_spinlock_lock(virtio_lock);
    
    // Allocate request header and status
    struct VirtioBlkReq* req = VMemAlloc(sizeof(struct VirtioBlkReq));
    uint8_t* status = VMemAlloc(1);
    
    if (!req || !status) {
        if (req) VMemFree(req, sizeof(struct VirtioBlkReq));
        if (status) VMemFree(status, 1);
        rust_spinlock_unlock(virtio_lock);
        return -1;
    }
    
    req->type = VIRTIO_BLK_T_OUT;
    req->reserved = 0;
    req->sector = sector;
    *status = 0xFF;
    
    // Set up descriptor chain
    uint16_t desc_idx = vq_next_desc_idx;
    
    // Descriptor 0: Request header (device reads)
    vq_desc_table[desc_idx].addr = VMemGetPhysAddr((uint64_t)req);
    vq_desc_table[desc_idx].len = sizeof(struct VirtioBlkReq);
    vq_desc_table[desc_idx].flags = VIRTQ_DESC_F_NEXT;
    vq_desc_table[desc_idx].next = (desc_idx + 1) % vq_size;
    
    // Descriptor 1: Data buffer (device reads)
    vq_desc_table[(desc_idx + 1) % vq_size].addr = VMemGetPhysAddr((uint64_t)buffer);
    vq_desc_table[(desc_idx + 1) % vq_size].len = count * 512;
    vq_desc_table[(desc_idx + 1) % vq_size].flags = VIRTQ_DESC_F_NEXT;
    vq_desc_table[(desc_idx + 1) % vq_size].next = (desc_idx + 2) % vq_size;
    
    // Descriptor 2: Status byte (device writes)
    vq_desc_table[(desc_idx + 2) % vq_size].addr = VMemGetPhysAddr((uint64_t)status);
    vq_desc_table[(desc_idx + 2) % vq_size].len = 1;
    vq_desc_table[(desc_idx + 2) % vq_size].flags = VIRTQ_DESC_F_WRITE;
    vq_desc_table[(desc_idx + 2) % vq_size].next = 0;
    
    // Add to available ring
    uint16_t avail_idx = vq_avail_ring->idx % vq_size;
    vq_avail_ring->ring[avail_idx] = desc_idx;
    vq_avail_ring->idx++;
    
    // Notify device
    if (notify_ptr) {
        *notify_ptr = 0;
    }
    
    // Wait for completion (simple polling)
    while (vq_used_ring->idx == last_used_idx) {
        __asm__ volatile("pause");
    }
    
    last_used_idx = vq_used_ring->idx;
    vq_next_desc_idx = (desc_idx + 3) % vq_size;
    
    int result = (*status == 0) ? 0 : -1;
    
    VMemFree(req, sizeof(struct VirtioBlkReq));
    VMemFree(status, 1);
    rust_spinlock_unlock(virtio_lock);
    
    return result;
}
