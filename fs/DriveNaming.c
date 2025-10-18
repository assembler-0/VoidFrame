#include "DriveNaming.h"
#include "Format.h"

static int ide_count = 0;
static int ahci_count = 0;
static int nvme_count = 0;
static int virtio_count = 0;

static char drive_name_buffer[16];

const char* GenerateDriveName(BlockDeviceType type) {
    switch (type) {
        case DEVICE_TYPE_IDE:
            FormatA(drive_name_buffer, sizeof(drive_name_buffer), "hd%c", 'a' + ide_count++);
            break;
        case DEVICE_TYPE_AHCI:
            FormatA(drive_name_buffer, sizeof(drive_name_buffer), "sd%c", 'a' + ahci_count++);
            break;
        case DEVICE_TYPE_NVME:
            FormatA(drive_name_buffer, sizeof(drive_name_buffer), "nvme%d", nvme_count++);
            break;
        case DEVICE_TYPE_VIRTIO:
            FormatA(drive_name_buffer, sizeof(drive_name_buffer), "vd%c", 'a' + virtio_count++);
            break;
        default:
            FormatA(drive_name_buffer, sizeof(drive_name_buffer), "unk%d", 0);
            break;
    }
    return drive_name_buffer;
}