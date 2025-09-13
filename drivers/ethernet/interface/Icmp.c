#include "Icmp.h"
#include "Ip.h"
#include "Console.h"
#include "KernelHeap.h"
#include "MemOps.h"

static uint16_t IcmpChecksum(const void* data, size_t length) {
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

void IcmpHandlePacket(const IpHeader* ip_header, const IcmpHeader* icmp_header, uint16_t len) {
    if (len < sizeof(IcmpHeader)) {
        return;
    }

    if (icmp_header->type == ICMP_TYPE_ECHO_REQUEST) {
        PrintKernel("ICMP: Echo request received.\n");

        // Create a reply
        uint8_t* reply_packet = KernelMemoryAlloc(len);
        IcmpHeader* reply_header = (IcmpHeader*)reply_packet;

        reply_header->type = ICMP_TYPE_ECHO_REPLY;
        reply_header->code = 0;
        reply_header->identifier = icmp_header->identifier;
        reply_header->sequence_number = icmp_header->sequence_number;

        // Copy data from original packet
        FastMemcpy(reply_packet + sizeof(IcmpHeader), (uint8_t*)icmp_header + sizeof(IcmpHeader), len - sizeof(IcmpHeader));

        // Calculate checksum
        reply_header->checksum = 0;
        reply_header->checksum = IcmpChecksum(reply_packet, len);

        // Send the reply
        IpSend(ip_header->src_ip, IP_PROTOCOL_ICMP, reply_packet, len);

        KernelFree(reply_packet);
    } else if (icmp_header->type == ICMP_TYPE_ECHO_REPLY) {
        PrintKernel("ICMP: Echo reply received!\n");
    }
}

void IcmpSendEchoRequest(uint8_t dest_ip[4]) {
    uint16_t packet_size = sizeof(IcmpHeader) + 32; // 32 bytes of data
    uint8_t* packet = KernelMemoryAlloc(packet_size);
    IcmpHeader* icmp_header = (IcmpHeader*)packet;

    icmp_header->type = ICMP_TYPE_ECHO_REQUEST;
    icmp_header->code = 0;
    icmp_header->identifier = HTONS(1234); // Arbitrary identifier
    icmp_header->sequence_number = HTONS(1); // Simple sequence number

    // Fill data with some pattern
    for (int i = 0; i < 32; i++) {
        packet[sizeof(IcmpHeader) + i] = i;
    }

    // Calculate checksum
    icmp_header->checksum = 0;
    icmp_header->checksum = IcmpChecksum(packet, packet_size);

    PrintKernel("Sending ICMP Echo Request...\n");
    IpSend(dest_ip, IP_PROTOCOL_ICMP, packet, packet_size);

    KernelFree(packet);
}
