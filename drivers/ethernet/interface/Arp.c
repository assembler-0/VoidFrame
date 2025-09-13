#include "Arp.h"
#include "../Network.h"
#include "Console.h"
#include "Ip.h"
#include "MemOps.h"
#include "stdbool.h"

static ArpCacheEntry g_arp_cache[ARP_CACHE_SIZE];

void ArpInit(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        g_arp_cache[i].state = ARP_STATE_EMPTY;
    }
    PrintKernel("ARP cache initialized.\n");
}

static void ArpSendRequest(uint8_t ip[4]) {
    NetworkDevice* net_dev = Net_GetDevice(0);
    if (!net_dev) return;

    FullArpPacket packet;

    // Ethernet Header
    memset(packet.eth.dest_mac, 0xFF, 6); // Broadcast
    const uint8_t* src_mac = net_dev->get_mac_address();
    FastMemcpy(packet.eth.src_mac, src_mac, 6);
    packet.eth.ethertype = HTONS(0x0806); // ARP

    // ARP Packet
    packet.arp.hardware_type = HTONS(1); // Ethernet
    packet.arp.protocol_type = HTONS(0x0800); // IPv4
    packet.arp.hardware_addr_len = 6;
    packet.arp.protocol_addr_len = 4;
    packet.arp.opcode = HTONS(1); // Request

    FastMemcpy(packet.arp.sender_mac, src_mac, 6);
    // Hardcoded IP for now
    packet.arp.sender_ip[0] = 10;
    packet.arp.sender_ip[1] = 0;
    packet.arp.sender_ip[2] = 2;
    packet.arp.sender_ip[3] = 15;

    memset(packet.arp.target_mac, 0, 6);
    FastMemcpy(packet.arp.target_ip, ip, 4);

    net_dev->send_packet(&packet, sizeof(FullArpPacket));
    PrintKernel("ARP request sent for IP: ");
    // Print IP
    PrintKernel("\n");
}

bool ArpResolve(uint8_t ip[4], uint8_t out_mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].state == ARP_STATE_RESOLVED && FastMemcmp(g_arp_cache[i].ip, ip, 4) == 0) {
            FastMemcpy(out_mac, g_arp_cache[i].mac, 6);
            return true;
        }
    }

    // Not in cache, send a request
    ArpSendRequest(ip);
    return false;
}

void ArpHandlePacket(const EthernetHeader* eth_header, uint16_t len) {
    if (len < sizeof(EthernetHeader) + sizeof(ArpPacket)) {
        return;
    }

    const ArpPacket* arp_packet = (const ArpPacket*)((uint8_t*)eth_header + sizeof(EthernetHeader));

    if (arp_packet->opcode == HTONS(2)) { // Reply
        // Add to cache
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (g_arp_cache[i].state == ARP_STATE_EMPTY) {
                g_arp_cache[i].state = ARP_STATE_RESOLVED;
                FastMemcpy(g_arp_cache[i].ip, arp_packet->sender_ip, 4);
                FastMemcpy(g_arp_cache[i].mac, arp_packet->sender_mac, 6);
                PrintKernel("ARP reply received, added to cache.\n");
                return;
            }
        }
    }
}
