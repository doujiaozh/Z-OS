#include "dns.h"
#include "lib.h"
#include "net.h"
#include "string.h"
#include "timer.h"
#include "udp.h"

#define DNS_CACHE_SIZE 8
#define DNS_CACHE_NAME 64
#define DNS_NEGATIVE_TTL 5
#define DNS_DEFAULT_TTL 60

struct dns_cache_entry {
    char name[DNS_CACHE_NAME];
    u32 address;
    u64 expiry;
    u32 ttl;
    bool negative;
    bool valid;
};

static struct dns_cache_entry cache[DNS_CACHE_SIZE];
static u16 query_identifier = 0x2000;
static u32 queries_sent = 0;
static u32 responses_received = 0;
static u32 failures = 0;
static u32 cache_hits = 0;
static u32 cache_misses = 0;

void dns_cache_clear(void) {
    memset(cache, 0, sizeof(cache));
}

u32 dns_cache_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        if (cache[index].valid) {
            count++;
        }
    }
    return count;
}

u32 dns_cache_negative_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        if (cache[index].valid && cache[index].negative) {
            count++;
        }
    }
    return count;
}

u32 dns_cache_hits(void) {
    return cache_hits;
}

u32 dns_cache_misses(void) {
    return cache_misses;
}

static void cache_store(const char *name, u32 address, u32 ttl, bool negative) {
    if (name == NULL || strlen(name) >= DNS_CACHE_NAME) {
        return;
    }
    u64 now = timer_uptime_ms();
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        if (cache[index].valid && strcmp(cache[index].name, name) == 0) {
            cache[index].address = address;
            cache[index].ttl = ttl;
            cache[index].negative = negative;
            cache[index].expiry = now + (u64)ttl * 1000;
            return;
        }
    }
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        if (!cache[index].valid) {
            strncpy(cache[index].name, name, DNS_CACHE_NAME - 1);
            cache[index].name[DNS_CACHE_NAME - 1] = 0;
            cache[index].address = address;
            cache[index].ttl = ttl;
            cache[index].negative = negative;
            cache[index].expiry = now + (u64)ttl * 1000;
            cache[index].valid = true;
            return;
        }
    }
    u32 oldest = 0;
    for (u32 index = 1; index < DNS_CACHE_SIZE; index++) {
        if (cache[index].expiry < cache[oldest].expiry) {
            oldest = index;
        }
    }
    strncpy(cache[oldest].name, name, DNS_CACHE_NAME - 1);
    cache[oldest].name[DNS_CACHE_NAME - 1] = 0;
    cache[oldest].address = address;
    cache[oldest].ttl = ttl;
    cache[oldest].negative = negative;
    cache[oldest].expiry = now + (u64)ttl * 1000;
    cache[oldest].valid = true;
}

bool dns_cache_insert(const char *name, u32 address, u32 ttl, bool negative) {
    if (name == NULL || ttl == 0) {
        return false;
    }
    cache_store(name, address, ttl, negative);
    return true;
}

bool dns_cache_lookup(const char *name, u32 *address, bool *negative) {
    if (name == NULL) {
        return false;
    }
    u64 now = timer_uptime_ms();
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        if (!cache[index].valid) {
            continue;
        }
        if (strcmp(cache[index].name, name) != 0) {
            continue;
        }
        if (cache[index].expiry <= now) {
            cache[index].valid = false;
            return false;
        }
        if (negative != NULL) {
            *negative = cache[index].negative;
        }
        if (address != NULL && !cache[index].negative) {
            *address = cache[index].address;
        }
        return true;
    }
    return false;
}

void dns_cache_expire_all(void) {
    for (u32 index = 0; index < DNS_CACHE_SIZE; index++) {
        cache[index].valid = false;
    }
}

bool dns_cache_entry(u32 index, char *name, u32 name_capacity, u32 *address, bool *negative) {
    if (index >= DNS_CACHE_SIZE || !cache[index].valid) {
        return false;
    }
    if (name != NULL && name_capacity > 0) {
        strncpy(name, cache[index].name, name_capacity - 1);
        name[name_capacity - 1] = 0;
    }
    if (address != NULL) {
        *address = cache[index].address;
    }
    if (negative != NULL) {
        *negative = cache[index].negative;
    }
    return true;
}

static void write_be16(u8 *data, u16 value) {
    data[0] = (u8)(value >> 8);
    data[1] = (u8)(value & 0xFF);
}

static u16 read_be16(const u8 *data) {
    return (u16)(((u16)data[0] << 8) | data[1]);
}

u32 dns_build_query(const char *name, u8 *buffer, u32 capacity) {
    if (name == NULL || buffer == NULL || capacity < 18) {
        return 0;
    }
    u32 name_length = (u32)strlen(name);
    if (name_length == 0 || name_length >= DNS_MAX_NAME) {
        return 0;
    }
    u32 position = 0;
    write_be16(buffer + position, query_identifier);
    write_be16(buffer + position + 2, 0x0100);
    write_be16(buffer + position + 4, 1);
    write_be16(buffer + position + 6, 0);
    write_be16(buffer + position + 8, 0);
    write_be16(buffer + position + 10, 0);
    position += 12;
    u32 label_start = 0;
    for (u32 index = 0; index <= name_length; index++) {
        if (index == name_length || name[index] == '.') {
            u32 label_length = index - label_start;
            if (label_length == 0 || label_length > 63) {
                return 0;
            }
            if (position + label_length + 1 >= capacity) {
                return 0;
            }
            buffer[position++] = (u8)label_length;
            memcpy(buffer + position, name + label_start, label_length);
            position += label_length;
            label_start = index + 1;
        }
    }
    buffer[position++] = 0;
    if (position + 4 > capacity) {
        return 0;
    }
    write_be16(buffer + position, DNS_TYPE_A);
    write_be16(buffer + position + 2, DNS_CLASS_IN);
    position += 4;
    return position;
}

static u32 skip_name(const u8 *message, u32 length, u32 position) {
    while (position < length) {
        u8 value = message[position];
        if ((value & 0xC0) == 0xC0) {
            return position + 2;
        }
        if (value == 0) {
            return position + 1;
        }
        position += (u32)value + 1;
    }
    return length;
}

bool dns_parse_response_full(const u8 *message, u32 length, u16 identifier, u32 *address,
                             u32 *ttl) {
    if (message == NULL || length < 12) {
        return false;
    }
    if (read_be16(message) != identifier) {
        return false;
    }
    u16 flags = read_be16(message + 2);
    if ((flags & 0x8000) == 0) {
        return false;
    }
    if ((flags & 0x000F) != 0) {
        return false;
    }
    u16 question_count = read_be16(message + 4);
    u16 answer_count = read_be16(message + 6);
    u32 position = 12;
    for (u16 index = 0; index < question_count; index++) {
        position = skip_name(message, length, position);
        if (position + 4 > length) {
            return false;
        }
        position += 4;
    }
    for (u16 index = 0; index < answer_count; index++) {
        position = skip_name(message, length, position);
        if (position + 10 > length) {
            return false;
        }
        u16 type = read_be16(message + position);
        u16 class_code = read_be16(message + position + 2);
        u32 record_ttl = ((u32)message[position + 4] << 24) | ((u32)message[position + 5] << 16) |
                         ((u32)message[position + 6] << 8) | message[position + 7];
        u16 data_length = read_be16(message + position + 8);
        position += 10;
        if (position + data_length > length) {
            return false;
        }
        if (type == DNS_TYPE_A && class_code == DNS_CLASS_IN && data_length == 4) {
            u32 result = ((u32)message[position] << 24) | ((u32)message[position + 1] << 16) |
                         ((u32)message[position + 2] << 8) | message[position + 3];
            if (address != NULL) {
                *address = result;
            }
            if (ttl != NULL) {
                *ttl = record_ttl;
            }
            return true;
        }
        position += data_length;
    }
    return false;
}

bool dns_parse_response(const u8 *message, u32 length, u16 identifier, u32 *address) {
    return dns_parse_response_full(message, length, identifier, address, NULL);
}

int dns_resolve(const char *name, u32 *address, u32 timeout_ms) {
    if (!net_ready() || name == NULL) {
        return -1;
    }
    bool negative = false;
    u32 cached = 0;
    if (dns_cache_lookup(name, &cached, &negative)) {
        cache_hits++;
        if (negative) {
            return -1;
        }
        if (address != NULL) {
            *address = cached;
        }
        return 0;
    }
    cache_misses++;
    query_identifier++;
    u8 query[DNS_MAX_MESSAGE];
    u32 query_length = dns_build_query(name, query, sizeof(query));
    if (query_length == 0) {
        failures++;
        return -1;
    }
    u16 identifier = read_be16(query);
    int udp_index = -1;
    for (u32 attempt = 0; attempt < 8; attempt++) {
        u16 port = (u16)(5300 + ((identifier + attempt * 37) % 512));
        udp_index = udp_bind(port);
        if (udp_index >= 0) {
            break;
        }
    }
    if (udp_index < 0) {
        failures++;
        return -1;
    }
    u32 server = net_config_get()->dns == 0 ? DNS_SERVER_IP : net_config_get()->dns;
    u16 server_port = net_config_get()->dns_port == 0 ? DNS_SERVER_PORT : net_config_get()->dns_port;
    if (udp_sendto((u32)udp_index, server, server_port, query, query_length) != 0) {
        udp_unbind(udp_socket_port((u32)udp_index));
        failures++;
        return -1;
    }
    queries_sent++;
    u8 response[DNS_MAX_MESSAGE];
    u32 source_ip = 0;
    u16 source_port = 0;
    int received = udp_recv((u32)udp_index, response, sizeof(response), timeout_ms,
                            &source_ip, &source_port);
    udp_unbind(udp_socket_port((u32)udp_index));
    if (received <= 0) {
        failures++;
        dns_cache_insert(name, 0, DNS_NEGATIVE_TTL, true);
        return -1;
    }
    responses_received++;
    u32 resolved = 0;
    u32 record_ttl = 0;
    if (!dns_parse_response_full(response, (u32)received, identifier, &resolved, &record_ttl)) {
        failures++;
        dns_cache_insert(name, 0, DNS_NEGATIVE_TTL, true);
        return -1;
    }
    dns_cache_insert(name, resolved, record_ttl == 0 ? DNS_DEFAULT_TTL : record_ttl, false);
    if (address != NULL) {
        *address = resolved;
    }
    return 0;
}

u32 dns_queries_sent(void) {
    return queries_sent;
}

u32 dns_responses_received(void) {
    return responses_received;
}

u32 dns_failures(void) {
    return failures;
}
