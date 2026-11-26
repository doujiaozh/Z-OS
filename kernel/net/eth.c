#include "eth.h"
#include "arp.h"
#include "e1000.h"
#include "ipv4.h"
#include "lib.h"
#include "net.h"
#include "string.h"

static u32 frames_dropped = 0;
static u32 arp_frames = 0;
static u32 ipv4_frames = 0;
static u32 other_frames = 0;

void eth_init(void) {
    frames_dropped = 0;
    arp_frames = 0;
    ipv4_frames = 0;
    other_frames = 0;
}

bool eth_parse_header(const u8 *frame, u32 length, struct eth_header *header) {
    if (frame == NULL || header == NULL || length < ETH_HEADER_SIZE) {
        return false;
    }
    memcpy(header->destination, frame, ETH_ADDRESS_SIZE);
    memcpy(header->source, frame + ETH_ADDRESS_SIZE, ETH_ADDRESS_SIZE);
    header->type = (u16)(((u16)frame[12] << 8) | frame[13]);
    return true;
}

int eth_send(const u8 *destination, u16 type, const void *payload, u32 length) {
    if (destination == NULL || payload == NULL || length == 0) {
        return -1;
    }
    if (length + ETH_HEADER_SIZE > NET_MTU + ETH_HEADER_SIZE) {
        return -1;
    }
    static u8 frame[NET_MTU + ETH_HEADER_SIZE];
    memcpy(frame, destination, ETH_ADDRESS_SIZE);
    const struct net_config *config = net_config_get();
    memcpy(frame + ETH_ADDRESS_SIZE, config->mac, ETH_ADDRESS_SIZE);
    frame[12] = (u8)(type >> 8);
    frame[13] = (u8)(type & 0xFF);
    memcpy(frame + ETH_HEADER_SIZE, payload, length);
    u32 total = length + ETH_HEADER_SIZE;
    if (e1000_transmit(frame, total) != 0) {
        frames_dropped++;
        return -1;
    }
    net_account_transmit(total);
    return 0;
}

void eth_receive(const u8 *frame, u32 length) {
    struct eth_header header;
    if (!eth_parse_header(frame, length, &header)) {
        frames_dropped++;
        return;
    }
    if (header.type == ETH_TYPE_ARP) {
        arp_frames++;
        arp_receive(frame, length);
        return;
    }
    if (header.type == ETH_TYPE_IPV4) {
        ipv4_frames++;
        ipv4_receive(frame, length);
        return;
    }
    other_frames++;
}

u32 eth_frames_dropped(void) {
    return frames_dropped;
}

u32 eth_frames_dispatched(u16 type) {
    if (type == ETH_TYPE_ARP) {
        return arp_frames;
    }
    if (type == ETH_TYPE_IPV4) {
        return ipv4_frames;
    }
    return other_frames;
}
