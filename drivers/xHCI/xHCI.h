#ifndef XHCI_H
#define XHCI_H

#include "PCI/PCI.h" // Assuming this is your PCI header
#include "stdint.h"

typedef struct {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} XhciTRB; // transfer request block

typedef struct {
    uint64_t address;
    uint32_t size;
    uint32_t reserved;
} XhciERSTEntry; // Event Ring Segment Table Entry

// Device Context structures
typedef struct {
    uint32_t route_string:20;
    uint32_t speed:4;
    uint32_t reserved1:1;
    uint32_t mtt:1;
    uint32_t hub:1;
    uint32_t context_entries:5;
    uint32_t max_exit_latency:16;
    uint32_t root_hub_port_number:8;
    uint32_t num_ports:8;
    uint32_t tt_hub_slot_id:8;
    uint32_t tt_port_number:8;
    uint32_t tt_think_time:2;
    uint32_t reserved2:4;
    uint32_t interrupter_target:10;
    uint32_t usb_device_address:8;
    uint32_t reserved3:19;
    uint32_t slot_state:5;
    uint32_t reserved4[4];
} XhciSlotContext;

typedef struct {
    uint32_t ep_state:3;
    uint32_t reserved1:5;
    uint32_t mult:2;
    uint32_t max_p_streams:5;
    uint32_t lsa:1;
    uint32_t interval:8;
    uint32_t max_esit_payload_hi:8;
    uint32_t reserved2:1;
    uint32_t error_count:2;
    uint32_t ep_type:3;
    uint32_t reserved3:1;
    uint32_t hid:1;
    uint32_t max_burst_size:8;
    uint32_t max_packet_size:16;
    uint64_t tr_dequeue_pointer;
    uint32_t average_trb_length:16;
    uint32_t max_esit_payload_lo:16;
    uint32_t reserved4[3];
} XhciEndpointContext;

typedef struct {
    XhciSlotContext slot;
    XhciEndpointContext endpoints[31]; // EP0 through EP15 IN/OUT
} XhciDeviceContext;

typedef struct {
    PciDevice pci_device;
    volatile uint8_t* mmio_base;
    volatile uint32_t* operational_regs;
    volatile uint32_t* runtime_regs;
    uint64_t mmio_size;
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t max_intrs;

    // Ring structures
    XhciTRB* command_ring;
    XhciTRB* event_ring;
    XhciERSTEntry* erst;
    uint64_t* dcbaa; // Device Context Base Address Array

    // Ring management
    uint32_t command_ring_enqueue;
    uint32_t command_ring_cycle;
    uint32_t event_ring_dequeue;
    uint32_t event_ring_cycle;

    // Device contexts (dynamically allocated as needed)
    XhciDeviceContext** device_contexts;
} XhciController;

// USB Setup Packet structure
typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} USBSetupPacket;

// Device descriptor structure
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} USBDeviceDescriptor;

// Configuration descriptor structure
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} USBConfigDescriptor;

// Interface descriptor structure
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} USBInterfaceDescriptor;

// Endpoint descriptor structure
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} USBEndpointDescriptor;

// USB Request Types
#define USB_REQ_GET_STATUS       0
#define USB_REQ_CLEAR_FEATURE    1
#define USB_REQ_SET_FEATURE      3
#define USB_REQ_SET_ADDRESS      5
#define USB_REQ_GET_DESCRIPTOR   6
#define USB_REQ_SET_DESCRIPTOR   7
#define USB_REQ_GET_CONFIG       8
#define USB_REQ_SET_CONFIG       9

// USB Descriptor Types
#define USB_DESC_DEVICE          1
#define USB_DESC_CONFIG          2
#define USB_DESC_STRING          3
#define USB_DESC_INTERFACE       4
#define USB_DESC_ENDPOINT        5

// USB Request Type bits
#define USB_REQTYPE_DIR_OUT      0x00
#define USB_REQTYPE_DIR_IN       0x80
#define USB_REQTYPE_TYPE_STD     0x00
#define USB_REQTYPE_TYPE_CLASS   0x20
#define USB_REQTYPE_TYPE_VENDOR  0x40
#define USB_REQTYPE_RECIP_DEVICE 0x00
#define USB_REQTYPE_RECIP_IFACE  0x01
#define USB_REQTYPE_RECIP_EP     0x02

// Function declarations
void xHCIInit();
int xHCIControllerInit(XhciController* controller, const PciDevice* pci_dev);
void xHCIControllerCleanup(XhciController* controller);

// Command functions
int xhci_enable_slot(XhciController* controller);
int xhci_address_device(XhciController* controller, uint8_t slot_id);
int xhci_configure_endpoint(XhciController* controller, uint8_t slot_id);

// Transfer functions
int xhci_control_transfer(XhciController* controller, uint8_t slot_id,
                         USBSetupPacket* setup, void* data, uint16_t length);
int xhci_bulk_transfer(XhciController* controller, uint8_t slot_id,
                      uint8_t endpoint, void* data, uint32_t length, int direction);
void xhci_scan_and_enumerate_ports(XhciController* controller);

#endif // XHCI_H