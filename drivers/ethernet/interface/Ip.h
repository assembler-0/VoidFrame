#ifndef IP_H
#define IP_H

#include <stdint.h>
#include <ethernet/Packet.h>

#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17

// IPv4 Header
typedef struct {
    uint8_t version_ihl;       // Version (4 bits) + IHL (4 bits)
    uint8_t dscp_ecn;          // Differentiated Services Code Point (6 bits) + Explicit Congestion Notification (2 bits)
    uint16_t total_length;     // Total length of the packet
    uint16_t identification;   // Identification field
    uint16_t flags_fragment;   // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t ttl;               // Time to Live
    uint8_t protocol;          // Protocol (e.g., TCP, UDP, ICMP)
    uint16_t header_checksum;  // Header checksum
    uint8_t src_ip[4];
    uint8_t dest_ip[4];
} __attribute__((packed)) IpHeader;

_Static_assert(sizeof(IpHeader) == 20, "IpHeader must be 20 bytes");

// Function to send an IP packet
void IpSend(uint8_t dest_ip[4], uint8_t protocol, const void* data, uint16_t len);

// Function to handle a received IP packet
void IpHandlePacket(const IpHeader* ip_header, uint16_t len);

#endif // IP_H
