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
bool dns_parse_response_full(const u8 *message, u32 length, u16 identifier, u32 *address,
                             u32 *ttl);
bool dns_cache_insert(const char *name, u32 address, u32 ttl, bool negative);
bool dns_cache_lookup(const char *name, u32 *address, bool *negative);
void dns_cache_clear(void);
u32 dns_cache_count(void);
u32 dns_cache_negative_count(void);
u32 dns_cache_hits(void);
u32 dns_cache_misses(void);
void dns_cache_expire_all(void);
bool dns_cache_entry(u32 index, char *name, u32 name_capacity, u32 *address, bool *negative);
u32 dns_queries_sent(void);
u32 dns_responses_received(void);
u32 dns_failures(void);

#endif
