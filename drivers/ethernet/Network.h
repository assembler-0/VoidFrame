#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>

#define MAX_NETWORK_DEVICES 4

// Forward declaration
struct NetworkDevice;

// Function pointer types
typedef int (*send_packet_t)(const void* data, uint16_t len);
typedef const uint8_t* (*get_mac_t)(void);
typedef void (*poll_receive_t)(void);

// Represents a generic network device
typedef struct NetworkDevice {
    char name[32];
    send_packet_t send_packet;
    get_mac_t get_mac_address;
    poll_receive_t poll_receive;
} NetworkDevice;

// Public functions
void Net_Initialize(void);
void Net_RegisterDevice(const char* name, send_packet_t sender, get_mac_t mac_getter, poll_receive_t poller);
NetworkDevice* Net_GetDevice(int index);
void Net_Poll(void);

#endif // NETWORK_H
