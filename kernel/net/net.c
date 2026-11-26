#include "net.h"
#include "arp.h"
#include "e1000.h"
#include "eth.h"
#include "icmp.h"
#include "ipv4.h"
#include "lib.h"
#include "string.h"
#include "udp.h"

static struct net_config config;
static u32 frames_received = 0;
static u32 frames_sent = 0;
static u32 bytes_received = 0;
static u32 bytes_sent = 0;
static u32 poll_count = 0;
static u32 ipv4_sent_frames = 0;
static u32 ipv4_received_frames = 0;

u16 net_htons(u16 value) {
    return (u16)((value << 8) | (value >> 8));
}

u16 net_ntohs(u16 value) {
    return net_htons(value);
}

u32 net_htonl(u32 value) {
    return ((value & 0xFFU) << 24) | ((value & 0xFF00U) << 8) |
           ((value >> 8) & 0xFF00U) | ((value >> 24) & 0xFFU);
}

u32 net_ntohl(u32 value) {
    return net_htonl(value);
}

u32 net_checksum(const void *data, u32 length) {
    const u8 *bytes = (const u8 *)data;
    u32 sum = 0;
    for (u32 index = 0; index + 1 < length; index += 2) {
        sum += ((u32)bytes[index] << 8) | bytes[index + 1];
    }
    if ((length & 1) != 0) {
        sum += (u32)bytes[length - 1] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (~sum) & 0xFFFFU;
}

u32 net_parse_ip(const char *text) {
    if (text == NULL) {
        return 0;
    }
    u32 parts[4] = {0, 0, 0, 0};
    u32 part = 0;
    u32 value = 0;
    u32 digits = 0;
    for (const char *cursor = text; *cursor != 0; cursor++) {
        char character = *cursor;
        if (character >= '0' && character <= '9') {
            value = value * 10 + (u32)(character - '0');
            digits++;
            if (value > 255 || digits > 3) {
                return 0;
            }
        } else if (character == '.') {
            if (part >= 3 || digits == 0) {
                return 0;
            }
            parts[part++] = value;
            value = 0;
            digits = 0;
        } else {
            return 0;
        }
    }
    if (part != 3 || digits == 0) {
        return 0;
    }
    parts[3] = value;
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
}

void net_format_ip(u32 address, char *buffer, u32 capacity) {
    if (buffer == NULL || capacity == 0) {
        return;
    }
    ksnprintf(buffer, capacity, "%u.%u.%u.%u",
              (address >> 24) & 0xFF, (address >> 16) & 0xFF,
              (address >> 8) & 0xFF, address & 0xFF);
}

void net_format_mac(const u8 *mac, char *buffer, u32 capacity) {
    if (mac == NULL || buffer == NULL || capacity == 0) {
        return;
    }
    ksnprintf(buffer, capacity, "%02x:%02x:%02x:%02x:%02x:%02x",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void net_init(void) {
    memset(&config, 0, sizeof(config));
    frames_received = 0;
    frames_sent = 0;
    bytes_received = 0;
    bytes_sent = 0;
    poll_count = 0;
    ipv4_sent_frames = 0;
    ipv4_received_frames = 0;
    arp_init();
    ipv4_init();
    udp_init();
    icmp_init();
    if (e1000_init() != 0) {
        config.ready = false;
        return;
    }
    e1000_get_mac(config.mac);
    config.ip = NET_IPV4(10, 0, 2, 15);
    config.gateway = NET_IPV4(10, 0, 2, 2);
    config.netmask = NET_IPV4(255, 255, 255, 0);
    config.mtu = NET_MTU;
    config.ready = true;
}

bool net_ready(void) {
    return config.ready;
}

const struct net_config *net_config_get(void) {
    return &config;
}

void net_poll(void) {
    if (!config.ready) {
        return;
    }
    poll_count++;
    e1000_poll();
    u8 frame[1600];
    for (u32 spin = 0; spin < 16; spin++) {
        u32 length = e1000_dequeue_frame(frame, sizeof(frame));
        if (length == 0) {
            break;
        }
        net_receive_frame(frame, length);
    }
}

void net_receive_frame(const u8 *frame, u32 length) {
    if (frame == NULL || length < 14) {
        return;
    }
    frames_received++;
    bytes_received += length;
    eth_receive(frame, length);
}

u32 net_frames_received(void) {
    return frames_received;
}

u32 net_frames_sent(void) {
    return frames_sent;
}

u32 net_bytes_received(void) {
    return bytes_received;
}

u32 net_bytes_sent(void) {
    return bytes_sent;
}

u32 net_poll_count(void) {
    return poll_count;
}

u64 net_uptime_ticks(void) {
    return 0;
}

void net_account_transmit(u32 length) {
    frames_sent++;
    bytes_sent += length;
}

void net_account_ipv4_sent(void) {
    ipv4_sent_frames++;
}

void net_account_ipv4_received(void) {
    ipv4_received_frames++;
}

u32 net_ipv4_sent(void) {
    return ipv4_sent_frames;
}

u32 net_ipv4_received(void) {
    return ipv4_received_frames;
}
