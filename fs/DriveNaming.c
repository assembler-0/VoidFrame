#include "DriveNaming.h"
#include "Format.h"
#include "SpinlockRust.h"

static int ide_count = 0;
static int ahci_count = 0;
static int nvme_count = 0;
static int virtio_count = 0;
static RustSpinLock* dn_lock = NULL;

void GenerateDriveNameInto(BlockDeviceType type, char* out_name) {
    if (!dn_lock) rust_spinlock_new();
    else rust_spinlock_lock(dn_lock);
    switch (type) {
        case DEVICE_TYPE_IDE:
            snprintf(out_name, 16, "hd%c", 'a' + ide_count++);
            break;
        case DEVICE_TYPE_AHCI:
            snprintf(out_name, 16, "sd%c", 'a' + ahci_count++);
            break;
        case DEVICE_TYPE_NVME:
            snprintf(out_name, 16, "nvme%d", nvme_count++);
            break;
        case DEVICE_TYPE_VIRTIO:
            snprintf(out_name, 16, "vd%c", 'a' + virtio_count++);
            break;
        default:
            snprintf(out_name, 16, "unk%d", 0);
            break;
    }
    rust_spinlock_unlock(dn_lock);
}