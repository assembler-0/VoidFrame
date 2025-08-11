#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// Endianness conversion - crucial for network packets
// Network byte order is Big Endian, x86 is Little Endian.
#define HTONS(n) ((((n) & 0xFF) << 8) | (((n) & 0xFF00) >> 8))

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


#endif // PACKET_H