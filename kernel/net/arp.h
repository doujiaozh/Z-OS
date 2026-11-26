#ifndef Z_OS_ARP_H
#define Z_OS_ARP_H

#include "types.h"

#define ARP_CACHE_SIZE 8
#define ARP_HARDWARE_TYPE_ETHERNET 1
#define ARP_PROTOCOL_IPV4 0x0800
#define ARP_OPERATION_REQUEST 1
#define ARP_OPERATION_REPLY 2
#define ARP_PACKET_SIZE 28

struct arp_entry {
    u32 ip;
    u8 mac[6];
    bool valid;
    u32 age;
};

void arp_init(void);
bool arp_cache_insert(u32 ip, const u8 *mac);
bool arp_cache_lookup(u32 ip, u8 *mac);
bool arp_cache_remove(u32 ip);
void arp_cache_clear(void);
u32 arp_cache_count(void);
int arp_send_request(u32 ip);
int arp_send_reply(u32 ip, const u8 *mac);
void arp_receive(const u8 *frame, u32 length);
int arp_resolve(u32 ip, u8 *mac, u32 timeout_ms);
u32 arp_requests_sent(void);
u32 arp_replies_received(void);
u32 arp_cache_hits(void);
u32 arp_cache_misses(void);

#endif
