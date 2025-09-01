#ifndef VOIDFRAME_VIRTIO_H
#define VOIDFRAME_VIRTIO_H

#include "stdint.h"

// Common VirtIO definitions will go here.
// For example, PCI configuration registers, feature bits, status fields, etc.

#define VIRTIO_VENDOR_ID 0x1AF4

// VirtIO PCI Capability IDs
#define VIRTIO_CAP_COMMON_CFG   1
#define VIRTIO_CAP_NOTIFY_CFG   2
#define VIRTIO_CAP_ISR_CFG      3
#define VIRTIO_CAP_DEVICE_CFG   4
#define VIRTIO_CAP_PCI_CFG      5

// VirtIO PCI Capability Structure
struct VirtioPciCap {
    uint8_t cap_vndr;    // Generic PCI field: PCI_CAP_ID_VNDR
    uint8_t cap_next;    // Generic PCI field: next ptr
    uint8_t cap_len;     // Generic PCI field: capability length
    uint8_t cfg_type;    // Identifies the structure.
    uint8_t bar;         // Which BAR to find it.
    uint8_t padding[3];
    uint32_t offset;     // Offset within the BAR.
    uint32_t length;     // Length of the structure, in bytes.
} __attribute__((packed));

// VirtIO Common Configuration Structure (packed for direct mapping)

struct VirtioPciCommonCfg {
    // Device features
    uint32_t device_feature_select;
    uint32_t device_feature;
    // Driver features
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    // MS-X config
    uint16_t msix_config;
    // Number of queues
    uint16_t num_queues;
    // Device status
    uint8_t device_status;
    // Config generation
    uint8_t config_generation;
    // --- Queue Related --- 
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} __attribute__((packed));

// VirtIO Block Device Feature Bits
#define VIRTIO_BLK_F_RO 5 // Device is read-only

// --- Virtqueue Structures ---

#define VIRTQ_DESC_F_NEXT  1 // Buffer continues via the next field.
#define VIRTQ_DESC_F_WRITE 2 // Buffer is write-only (device-to-driver).

struct VirtqDesc {
    uint64_t addr;  // Physical address
    uint32_t len;
    uint16_t flags;
    uint16_t next;  // Index of next descriptor in chain
} __attribute__((packed));

struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; // Array of descriptor indices
} __attribute__((packed));

struct VirtqUsedElem {
    uint32_t id;    // Index of the head of the used descriptor chain
    uint32_t len;   // Total length of the bytes written
} __attribute__((packed));

struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;   // Where the device puts the next used descriptor index
    struct VirtqUsedElem ring[];
} __attribute__((packed));

// --- VirtIO Block Device Specifics ---

#define VIRTIO_BLK_T_IN  0 // Read request
#define VIRTIO_BLK_T_OUT 1 // Write request

struct VirtioBlkReq {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));


#endif //VOIDFRAME_VIRTIO_H
