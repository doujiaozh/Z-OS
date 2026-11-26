#ifndef Z_OS_ETH_H
#define Z_OS_ETH_H

#include "types.h"

#define ETH_HEADER_SIZE 14
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP 0x0806
#define ETH_ADDRESS_SIZE 6

struct eth_header {
    u8 destination[ETH_ADDRESS_SIZE];
    u8 source[ETH_ADDRESS_SIZE];
    u16 type;
};

void eth_init(void);
bool eth_parse_header(const u8 *frame, u32 length, struct eth_header *header);
int eth_send(const u8 *destination, u16 type, const void *payload, u32 length);
void eth_receive(const u8 *frame, u32 length);
u32 eth_frames_dropped(void);
u32 eth_frames_dispatched(u16 type);
void net_account_transmit(u32 length);

#endif
