#ifndef Z_OS_IPV4_H
#define Z_OS_IPV4_H

#include "types.h"

#define IPV4_HEADER_SIZE 20
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_UDP 17
#define IPV4_DEFAULT_TTL 64

struct ipv4_header_view {
    u8 version;
    u8 header_length;
    u16 total_length;
    u16 identification;
    u16 flags_fragment;
    u8 ttl;
    u8 protocol;
    u16 checksum;
    u32 source;
    u32 destination;
};

void ipv4_init(void);
u32 ipv4_checksum(const void *header, u32 length);
u32 ipv4_next_hop(u32 destination);
int ipv4_send(u32 destination, u8 protocol, const void *payload, u32 length);
bool ipv4_parse_header(const u8 *packet, u32 length, struct ipv4_header_view *view);
void ipv4_receive(const u8 *frame, u32 length);
u32 ipv4_frames_sent(void);
u32 ipv4_frames_received(void);
u32 ipv4_checksum_errors(void);

#endif
