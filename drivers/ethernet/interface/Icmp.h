#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include "Ip.h"

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence_number;
} __attribute__((packed)) IcmpHeader;

void IcmpHandlePacket(const IpHeader* ip_header, const IcmpHeader* icmp_header, uint16_t len);
void IcmpSendEchoRequest(uint8_t dest_ip[4]);

#endif // ICMP_H
