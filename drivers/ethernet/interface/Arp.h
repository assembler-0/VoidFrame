#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <../Packet.h>
#include <stdbool.h>

#define ARP_CACHE_SIZE 16

typedef enum {
    ARP_STATE_EMPTY,
    ARP_STATE_RESOLVING,
    ARP_STATE_RESOLVED
} ArpCacheEntryState;

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    ArpCacheEntryState state;
} ArpCacheEntry;

void ArpInit(void);
void ArpHandlePacket(const EthernetHeader* eth_header, uint16_t len);
// Returns true if the MAC is known, false if an ARP request was sent.
// The caller should then wait and retry.
bool ArpResolve(uint8_t ip[4], uint8_t out_mac[6]);

#endif // ARP_H
