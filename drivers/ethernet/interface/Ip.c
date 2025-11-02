#include <Ip.h>
#include <Network.h>
#include <Arp.h>
#include <Console.h>
#include <KernelHeap.h>
#include <MemOps.h>
#include <ethernet/interface/Icmp.h>


static uint16_t IpChecksum(const void* data, size_t length) {
    const uint16_t* buf = data;
    uint32_t sum = 0;
    while (length > 1) {
        sum += *buf++;
        length -= 2;
    }
    if (length > 0) {
        sum += *(const uint8_t*)buf;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void IpSend(uint8_t dest_ip[4], uint8_t protocol, const void* data, uint16_t len) {
    NetworkDevice* net_dev = NULL;
    for (int i = 0; i < MAX_NETWORK_DEVICES; i++) {
        net_dev = Net_GetDevice(i);
        if (net_dev) break;
    }

    uint8_t dest_mac[6];
    if (!ArpResolve(dest_ip, dest_mac)) {
        PrintKernel("IP: Destination MAC not in ARP cache, sending ARP request.\n");
        // For now, we don't handle queuing the packet. The packet is dropped.
        return;
    }

    uint16_t total_len = sizeof(IpHeader) + len;
    uint8_t* packet = KernelMemoryAlloc(total_len);
    IpHeader* ip_header = (IpHeader*)packet;

    // Fill IP header
    ip_header->version_ihl = (4 << 4) | 5; // IPv4, 5 * 32-bit words
    ip_header->dscp_ecn = 0;
    ip_header->total_length = HTONS(total_len);
    ip_header->identification = HTONS(1); // Simple static ID
    ip_header->flags_fragment = 0;
    ip_header->ttl = 64;
    ip_header->protocol = protocol;
    // Get our IP address (hardcoded for now)
    ip_header->src_ip[0] = 10;
    ip_header->src_ip[1] = 0;
    ip_header->src_ip[2] = 2;
    ip_header->src_ip[3] = 15;
    FastMemcpy(ip_header->dest_ip, dest_ip, 4);

    // Calculate checksum
    ip_header->header_checksum = 0;
    ip_header->header_checksum = IpChecksum(ip_header, sizeof(IpHeader));

    // Copy data
    FastMemcpy(packet + sizeof(IpHeader), data, len);

    // Now, wrap it in an Ethernet frame
    uint16_t eth_total_len = sizeof(EthernetHeader) + total_len;
    uint8_t* eth_frame = KernelMemoryAlloc(eth_total_len);
    EthernetHeader* eth_header = (EthernetHeader*)eth_frame;

    FastMemcpy(eth_header->dest_mac, dest_mac, 6);
    const uint8_t* src_mac = net_dev->get_mac_address();
    FastMemcpy(eth_header->src_mac, src_mac, 6);
    eth_header->ethertype = HTONS(0x0800); // IPv4

    FastMemcpy(eth_frame + sizeof(EthernetHeader), packet, total_len);

    // Send it!
    net_dev->send_packet(eth_frame, eth_total_len);

    KernelFree(packet);
    KernelFree(eth_frame);

    PrintKernel("IP: Sent packet!\n");
}

void IpHandlePacket(const IpHeader* ip_header, uint16_t len) {
    if (ip_header->protocol == IP_PROTOCOL_ICMP) {
        IcmpHandlePacket(ip_header, (const IcmpHeader*)((uint8_t*)ip_header + sizeof(IpHeader)), len - sizeof(IpHeader));
    } else {
        // For now, just print information about the received packet
        PrintKernel("IP: Received packet! Protocol: ");
        PrintKernelInt(ip_header->protocol);
        PrintKernel("\n");
    }
}