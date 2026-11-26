#ifndef Z_OS_NET_H
#define Z_OS_NET_H

#include "types.h"

#define NET_IPV4(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))
#define NET_ETHERTYPE_IPV4 0x0800
#define NET_ETHERTYPE_ARP 0x0806
#define NET_MTU 1500

struct net_config {
    bool ready;
    u8 mac[6];
    u32 ip;
    u32 gateway;
    u32 netmask;
    u32 dns;
    u16 dns_port;
    u16 mtu;
};

void net_init(void);
bool net_ready(void);
void net_poll(void);
bool net_in_poll(void);
void net_delay_ms(u32 milliseconds);
void net_receive_frame(const u8 *frame, u32 length);
const struct net_config *net_config_get(void);
u32 net_checksum(const void *data, u32 length);
u16 net_htons(u16 value);
u16 net_ntohs(u16 value);
u32 net_htonl(u32 value);
u32 net_ntohl(u32 value);
u32 net_parse_ip(const char *text);
void net_format_ip(u32 address, char *buffer, u32 capacity);
void net_format_mac(const u8 *mac, char *buffer, u32 capacity);
u32 net_frames_received(void);
u32 net_frames_sent(void);
u32 net_bytes_received(void);
u32 net_bytes_sent(void);
u32 net_poll_count(void);
u64 net_uptime_ticks(void);
u32 net_ipv4_sent(void);
u32 net_ipv4_received(void);
void net_account_transmit(u32 length);
void net_account_ipv4_sent(void);
void net_account_ipv4_received(void);

#endif
