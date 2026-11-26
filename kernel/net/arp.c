#include "arp.h"
#include "eth.h"
#include "lib.h"
#include "net.h"
#include "thread.h"
#include "string.h"

static struct arp_entry cache[ARP_CACHE_SIZE];
static u32 requests_sent = 0;
static u32 replies_received = 0;
static u32 cache_hits = 0;
static u32 cache_misses = 0;
static u32 cache_clock = 0;

void arp_init(void) {
    memset(cache, 0, sizeof(cache));
    requests_sent = 0;
    replies_received = 0;
    cache_hits = 0;
    cache_misses = 0;
    cache_clock = 0;
}

bool arp_cache_insert(u32 ip, const u8 *mac) {
    if (mac == NULL) {
        return false;
    }
    for (u32 index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].valid && cache[index].ip == ip) {
            memcpy(cache[index].mac, mac, 6);
            cache[index].age = ++cache_clock;
            return true;
        }
    }
    for (u32 index = 0; index < ARP_CACHE_SIZE; index++) {
        if (!cache[index].valid) {
            cache[index].ip = ip;
            memcpy(cache[index].mac, mac, 6);
            cache[index].valid = true;
            cache[index].age = ++cache_clock;
            return true;
        }
    }
    u32 oldest = 0;
    for (u32 index = 1; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].age < cache[oldest].age) {
            oldest = index;
        }
    }
    cache[oldest].ip = ip;
    memcpy(cache[oldest].mac, mac, 6);
    cache[oldest].valid = true;
    cache[oldest].age = ++cache_clock;
    return true;
}

bool arp_cache_lookup(u32 ip, u8 *mac) {
    for (u32 index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].valid && cache[index].ip == ip) {
            cache_hits++;
            if (mac != NULL) {
                memcpy(mac, cache[index].mac, 6);
            }
            return true;
        }
    }
    cache_misses++;
    return false;
}

bool arp_cache_remove(u32 ip) {
    for (u32 index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].valid && cache[index].ip == ip) {
            cache[index].valid = false;
            return true;
        }
    }
    return false;
}

void arp_cache_clear(void) {
    memset(cache, 0, sizeof(cache));
}

u32 arp_cache_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < ARP_CACHE_SIZE; index++) {
        if (cache[index].valid) {
            count++;
        }
    }
    return count;
}

static void arp_build_request(u8 *packet, u32 target_ip) {
    const struct net_config *config = net_config_get();
    memset(packet, 0, ARP_PACKET_SIZE);
    packet[0] = (u8)(ARP_HARDWARE_TYPE_ETHERNET >> 8);
    packet[1] = (u8)(ARP_HARDWARE_TYPE_ETHERNET & 0xFF);
    packet[2] = (u8)(ARP_PROTOCOL_IPV4 >> 8);
    packet[3] = (u8)(ARP_PROTOCOL_IPV4 & 0xFF);
    packet[4] = 6;
    packet[5] = 4;
    packet[6] = 0;
    packet[7] = ARP_OPERATION_REQUEST;
    memcpy(packet + 8, config->mac, 6);
    packet[14] = (u8)(config->ip >> 24);
    packet[15] = (u8)(config->ip >> 16);
    packet[16] = (u8)(config->ip >> 8);
    packet[17] = (u8)(config->ip & 0xFF);
    packet[24] = (u8)(target_ip >> 24);
    packet[25] = (u8)(target_ip >> 16);
    packet[26] = (u8)(target_ip >> 8);
    packet[27] = (u8)(target_ip & 0xFF);
}

int arp_send_request(u32 ip) {
    static const u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 packet[ARP_PACKET_SIZE];
    arp_build_request(packet, ip);
    if (eth_send(broadcast, ETH_TYPE_ARP, packet, ARP_PACKET_SIZE) != 0) {
        return -1;
    }
    requests_sent++;
    return 0;
}

int arp_send_reply(u32 ip, const u8 *mac) {
    if (mac == NULL) {
        return -1;
    }
    const struct net_config *config = net_config_get();
    u8 packet[ARP_PACKET_SIZE];
    memset(packet, 0, ARP_PACKET_SIZE);
    packet[0] = (u8)(ARP_HARDWARE_TYPE_ETHERNET >> 8);
    packet[1] = (u8)(ARP_HARDWARE_TYPE_ETHERNET & 0xFF);
    packet[2] = (u8)(ARP_PROTOCOL_IPV4 >> 8);
    packet[3] = (u8)(ARP_PROTOCOL_IPV4 & 0xFF);
    packet[4] = 6;
    packet[5] = 4;
    packet[6] = 0;
    packet[7] = ARP_OPERATION_REPLY;
    memcpy(packet + 8, config->mac, 6);
    packet[14] = (u8)(config->ip >> 24);
    packet[15] = (u8)(config->ip >> 16);
    packet[16] = (u8)(config->ip >> 8);
    packet[17] = (u8)(config->ip & 0xFF);
    memcpy(packet + 18, mac, 6);
    packet[24] = (u8)(ip >> 24);
    packet[25] = (u8)(ip >> 16);
    packet[26] = (u8)(ip >> 8);
    packet[27] = (u8)(ip & 0xFF);
    return eth_send(mac, ETH_TYPE_ARP, packet, ARP_PACKET_SIZE);
}

void arp_receive(const u8 *frame, u32 length) {
    if (frame == NULL || length < ETH_HEADER_SIZE + ARP_PACKET_SIZE) {
        return;
    }
    const u8 *packet = frame + ETH_HEADER_SIZE;
    u16 hardware_type = (u16)(((u16)packet[0] << 8) | packet[1]);
    u16 protocol = (u16)(((u16)packet[2] << 8) | packet[3]);
    u16 operation = (u16)(((u16)packet[6] << 8) | packet[7]);
    if (hardware_type != ARP_HARDWARE_TYPE_ETHERNET || protocol != ARP_PROTOCOL_IPV4) {
        return;
    }
    const u8 *sender_mac = packet + 8;
    u32 sender_ip = ((u32)packet[14] << 24) | ((u32)packet[15] << 16) |
                    ((u32)packet[16] << 8) | packet[17];
    u32 target_ip = ((u32)packet[24] << 24) | ((u32)packet[25] << 16) |
                    ((u32)packet[26] << 8) | packet[27];
    const struct net_config *config = net_config_get();
    if (operation == ARP_OPERATION_REPLY) {
        replies_received++;
        arp_cache_insert(sender_ip, sender_mac);
        return;
    }
    if (operation == ARP_OPERATION_REQUEST) {
        arp_cache_insert(sender_ip, sender_mac);
        if (target_ip == config->ip) {
            arp_send_reply(sender_ip, sender_mac);
        }
    }
}

int arp_resolve(u32 ip, u8 *mac, u32 timeout_ms) {
    if (arp_cache_lookup(ip, mac)) {
        return 0;
    }
    if (!net_ready()) {
        return -1;
    }
    if (arp_send_request(ip) != 0) {
        return -1;
    }
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        net_poll();
        if (arp_cache_lookup(ip, mac)) {
            return 0;
        }
        sched_sleep_ms(10);
    }
    return -1;
}

u32 arp_requests_sent(void) {
    return requests_sent;
}

u32 arp_replies_received(void) {
    return replies_received;
}

u32 arp_cache_hits(void) {
    return cache_hits;
}

u32 arp_cache_misses(void) {
    return cache_misses;
}
