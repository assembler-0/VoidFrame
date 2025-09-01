#include "VirtioBlk.h"
#include "Spinlock.h"
#include "Atomics.h"
#include "Console.h"
#include "PCI/PCI.h"
#include "VMem.h"
#include "stdbool.h"

// Globals to hold the capability structures we find
static volatile int* virtio_lock;
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
volatile struct VirtioPciCommonCfg* common_cfg_ptr;

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

// Implementation for the VirtIO Block device driver.

void InitializeVirtioBlk(PciDevice device) {
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
    common_cfg_ptr->device_status = 0; // 1. Reset

    common_cfg_ptr->device_status |= (1 << 0); // 2. Set ACKNOWLEDGE bit
    common_cfg_ptr->device_status |= (1 << 1); // 3. Set DRIVER bit

    // 4. Feature Negotiation
    common_cfg_ptr->driver_feature_select = 0;
    uint32_t device_features = common_cfg_ptr->device_feature;
    common_cfg_ptr->driver_feature_select = 0;
    common_cfg_ptr->driver_feature = 0;

    // 5. Set FEATURES_OK status bit
    common_cfg_ptr->device_status |= (1 << 3);

    // 6. Re-read status to ensure device accepted features
    uint8_t status = common_cfg_ptr->device_status;
    if (!(status & (1 << 3))) {
        PrintKernel("VirtIO-Blk: Error - Device rejected features!\n");
        return;
    }

    // --- Step 7: Virtqueue Setup ---
    common_cfg_ptr->queue_select = 0;

    vq_size = common_cfg_ptr->queue_size;
    if (vq_size == 0) {
        PrintKernel("VirtIO-Blk: Error - Queue 0 is not available.\n");
        return;
    }

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
    common_cfg_ptr->queue_desc = VIRT_TO_PHYS((uint64_t)vq_desc_table);
    common_cfg_ptr->queue_driver = VIRT_TO_PHYS((uint64_t)vq_avail_ring);
    common_cfg_ptr->queue_device = VIRT_TO_PHYS((uint64_t)vq_used_ring);

    // Enable the queue
    common_cfg_ptr->queue_enable = 1;

    PrintKernel("VirtIO-Blk: Driver initialized successfully!\n");
}

int VirtioBlkRead(uint64_t sector, void* buffer) {
    // Add synchronization for concurrent access
    SpinLock(virtio_lock);
    // Check if we have enough descriptors available
    if ((vq_next_desc_idx + 3) > vq_size) {
        PrintKernel("VirtIO-Blk: Error - Not enough descriptors available\n");
        // SpinUnlock(&virtio_lock);
        return -1;
    }
    // Step 1: Allocate request header and status byte
    struct VirtioBlkReq* req_hdr = VMemAlloc(sizeof(struct VirtioBlkReq));
    uint8_t* status = VMemAlloc(sizeof(uint8_t));
    if (!req_hdr || !status) {
        PrintKernel("VirtIO-Blk: Failed to allocate request header/status\n");
        if (req_hdr) VMemFree(req_hdr, sizeof(struct VirtioBlkReq));
        if (status)  VMemFree(status,  sizeof(uint8_t));
        // SpinUnlock(&virtio_lock);
        return -1; // Error
    }
    // Step 2: Fill out the request header
    req_hdr->type     = VIRTIO_BLK_T_IN;
    req_hdr->reserved = 0;
    req_hdr->sector   = sector;
    // Step 3: Set up the descriptor chain
    uint16_t head_idx = vq_next_desc_idx;
    // Descriptor 1: Request Header (Driver -> Device)
    vq_desc_table[head_idx].addr  = VIRT_TO_PHYS((uint64_t)req_hdr);
    vq_desc_table[head_idx].len   = sizeof(struct VirtioBlkReq);
    vq_desc_table[head_idx].flags = VIRTQ_DESC_F_NEXT;
    vq_desc_table[head_idx].next  = head_idx + 1;
    // Descriptor 2: Data Buffer (Device -> Driver)
    vq_desc_table[head_idx + 1].addr  = VIRT_TO_PHYS((uint64_t)buffer);
    vq_desc_table[head_idx + 1].len   = 512; // Assume 512-byte sectors
    vq_desc_table[head_idx + 1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    vq_desc_table[head_idx + 1].next  = head_idx + 2;
    // Descriptor 3: Status (Device -> Driver)
    vq_desc_table[head_idx + 2].addr  = VIRT_TO_PHYS((uint64_t)status);
    vq_desc_table[head_idx + 2].len   = sizeof(uint8_t);
    vq_desc_table[head_idx + 2].flags = VIRTQ_DESC_F_WRITE;
    vq_desc_table[head_idx + 2].next  = 0; // End of chain
    // Step 4: Add the chain to the available ring
    vq_avail_ring->ring[vq_avail_ring->idx % vq_size] = head_idx;
    // Step 5: Update the available ring index atomically
    AtomicInc(&vq_avail_ring->idx);
    // Step 6: Notify the device
    if (notify_ptr) {
        *(notify_ptr + common_cfg_ptr->queue_notify_off) = 0; // Queue index is 0
    }
    // For now, we just update our internal descriptor index
    vq_next_desc_idx = (vq_next_desc_idx + 3) % vq_size;
    // TODO: Implement proper completion waiting mechanism
    // This is critical for correctness
    // For now, add a delay to allow completion (not production-ready)
    // In production, use interrupts or polling with timeout
    // Don't free these until after checking completion!
    // These will need to be tracked and freed after the I/O completes
    SpinUnlock(virtio_lock);
    return 0; // Success (for now)
}