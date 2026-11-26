#ifndef Z_OS_ICMP_H
#define Z_OS_ICMP_H

#include "types.h"

#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_HEADER_SIZE 8

void icmp_init(void);
int icmp_send_echo_request(u32 destination, u16 identifier, u16 sequence);
int icmp_send_echo_reply(u32 destination, u16 identifier, u16 sequence, const u8 *data, u32 length);
void icmp_receive(const u8 *packet, u32 length, u32 source);
int icmp_ping(u32 destination, u32 timeout_ms, u32 *round_trip_ms);
bool icmp_reply_waiting(void);
void icmp_clear_reply(void);
u32 icmp_requests_sent(void);
u32 icmp_replies_received(void);
u32 icmp_requests_answered(void);

#endif
