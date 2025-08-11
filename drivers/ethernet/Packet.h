#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// Endianness conversion - crucial for network packets
// Network byte order is Big Endian, x86 is Little Endian.
static inline uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}
#define HTONS(x)  bswap16((uint16_t)(x))
#define NTOHS(x)  bswap16((uint16_t)(x))

// Ethernet Header (14 bytes)
typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed)) EthernetHeader;

// ARP Packet (28 bytes)
typedef struct {
    uint16_t hardware_type; // 1 for Ethernet
    uint16_t protocol_type; // 0x0800 for IPv4
    uint8_t hardware_addr_len; // 6 for MAC
    uint8_t protocol_addr_len; // 4 for IP
    uint16_t opcode; // 1 for request, 2 for reply
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} __attribute__((packed)) ArpPacket;

// Full ARP-over-Ethernet Packet (42 bytes)
typedef struct {
    EthernetHeader eth;
    ArpPacket arp;
} __attribute__((packed)) FullArpPacket;

_Static_assert(sizeof(EthernetHeader) == 14, "EthernetHeader must be 14 bytes");
_Static_assert(sizeof(ArpPacket) == 28, "ArpPacket must be 28 bytes");
_Static_assert(sizeof(FullArpPacket) == 42, "FullArpPacket must be 42 bytes");

#endif // PACKET_H