#include "ipv4.h"
#include "arp.h"
#include "eth.h"
#include "icmp.h"
#include "lib.h"
#include "net.h"
#include "string.h"
#include "udp.h"

static u16 identification = 1;
static u32 frames_sent = 0;
static u32 frames_received = 0;
static u32 checksum_errors = 0;

void ipv4_init(void) {
    identification = 1;
    frames_sent = 0;
    frames_received = 0;
    checksum_errors = 0;
}

u32 ipv4_checksum(const void *header, u32 length) {
    return net_checksum(header, length);
}

u32 ipv4_next_hop(u32 destination) {
    const struct net_config *config = net_config_get();
    if ((destination & config->netmask) == (config->ip & config->netmask)) {
        return destination;
    }
    return config->gateway;
}

bool ipv4_parse_header(const u8 *packet, u32 length, struct ipv4_header_view *view) {
    if (packet == NULL || view == NULL || length < IPV4_HEADER_SIZE) {
        return false;
    }
    view->version = (u8)(packet[0] >> 4);
    view->header_length = (u8)((packet[0] & 0x0F) * 4);
    view->total_length = (u16)(((u16)packet[2] << 8) | packet[3]);
    view->identification = (u16)(((u16)packet[4] << 8) | packet[5]);
    view->flags_fragment = (u16)(((u16)packet[6] << 8) | packet[7]);
    view->ttl = packet[8];
    view->protocol = packet[9];
    view->checksum = (u16)(((u16)packet[10] << 8) | packet[11]);
    view->source = ((u32)packet[12] << 24) | ((u32)packet[13] << 16) |
                   ((u32)packet[14] << 8) | packet[15];
    view->destination = ((u32)packet[16] << 24) | ((u32)packet[17] << 16) |
                        ((u32)packet[18] << 8) | packet[19];
    if (view->version != 4 || view->header_length < IPV4_HEADER_SIZE) {
        return false;
    }
    if (view->total_length < view->header_length || view->total_length > length) {
        return false;
    }
    if (net_checksum(packet, view->header_length) != 0) {
        checksum_errors++;
        return false;
    }
    return true;
}

int ipv4_send(u32 destination, u8 protocol, const void *payload, u32 length) {
    if (!net_ready() || payload == NULL || length == 0) {
        return -1;
    }
    if (length + IPV4_HEADER_SIZE > NET_MTU) {
        return -1;
    }
    u32 next_hop = ipv4_next_hop(destination);
    u8 next_mac[6];
    if (arp_resolve(next_hop, next_mac, 1000) != 0) {
        return -1;
    }
    static u8 packet[NET_MTU];
    memset(packet, 0, IPV4_HEADER_SIZE);
    u32 total = length + IPV4_HEADER_SIZE;
    packet[0] = 0x45;
    packet[1] = 0;
    packet[2] = (u8)(total >> 8);
    packet[3] = (u8)(total & 0xFF);
    packet[4] = (u8)(identification >> 8);
    packet[5] = (u8)(identification & 0xFF);
    identification++;
    packet[6] = 0x40;
    packet[7] = 0;
    packet[8] = IPV4_DEFAULT_TTL;
    packet[9] = protocol;
    const struct net_config *config = net_config_get();
    packet[12] = (u8)(config->ip >> 24);
    packet[13] = (u8)(config->ip >> 16);
    packet[14] = (u8)(config->ip >> 8);
    packet[15] = (u8)(config->ip & 0xFF);
    packet[16] = (u8)(destination >> 24);
    packet[17] = (u8)(destination >> 16);
    packet[18] = (u8)(destination >> 8);
    packet[19] = (u8)(destination & 0xFF);
    u16 checksum = (u16)net_checksum(packet, IPV4_HEADER_SIZE);
    packet[10] = (u8)(checksum >> 8);
    packet[11] = (u8)(checksum & 0xFF);
    memcpy(packet + IPV4_HEADER_SIZE, payload, length);
    if (eth_send(next_mac, ETH_TYPE_IPV4, packet, total) != 0) {
        return -1;
    }
    frames_sent++;
    net_account_ipv4_sent();
    return 0;
}

void ipv4_receive(const u8 *frame, u32 length) {
    if (frame == NULL || length < ETH_HEADER_SIZE + IPV4_HEADER_SIZE) {
        return;
    }
    const u8 *packet = frame + ETH_HEADER_SIZE;
    u32 available = length - ETH_HEADER_SIZE;
    struct ipv4_header_view view;
    if (!ipv4_parse_header(packet, available, &view)) {
        return;
    }
    const struct net_config *config = net_config_get();
    u32 broadcast = config->ip | ~config->netmask;
    if (view.destination != config->ip && view.destination != broadcast &&
        view.destination != 0xFFFFFFFFU) {
        return;
    }
    if ((view.flags_fragment & 0x1FFF) != 0) {
        return;
    }
    frames_received++;
    net_account_ipv4_received();
    const u8 *payload = packet + view.header_length;
    u32 payload_length = view.total_length - view.header_length;
    if (view.protocol == IPV4_PROTOCOL_ICMP) {
        icmp_receive(payload, payload_length, view.source);
    } else if (view.protocol == IPV4_PROTOCOL_UDP) {
        udp_receive(payload, payload_length, view.source);
    }
}

u32 ipv4_frames_sent(void) {
    return frames_sent;
}

u32 ipv4_frames_received(void) {
    return frames_received;
}

u32 ipv4_checksum_errors(void) {
    return checksum_errors;
}
