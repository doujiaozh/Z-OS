#ifndef Z_OS_DNS_H
#define Z_OS_DNS_H

#include "types.h"

#define DNS_SERVER_IP ((10U << 24) | (0U << 16) | (2U << 8) | 3U)
#define DNS_SERVER_PORT 53
#define DNS_MAX_NAME 128
#define DNS_MAX_MESSAGE 512
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

int dns_resolve(const char *name, u32 *address, u32 timeout_ms);
u32 dns_build_query(const char *name, u8 *buffer, u32 capacity);
bool dns_parse_response(const u8 *message, u32 length, u16 identifier, u32 *address);
u32 dns_queries_sent(void);
u32 dns_responses_received(void);
u32 dns_failures(void);

#endif
