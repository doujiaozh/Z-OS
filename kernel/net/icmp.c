#include "icmp.h"
#include "ipv4.h"
#include "lib.h"
#include "net.h"
#include "thread.h"
#include "string.h"

static u16 next_identifier = 0x5A4F;
static u16 next_sequence = 1;
static u32 requests_sent = 0;
static u32 replies_received = 0;
static u32 requests_answered = 0;
static bool reply_waiting = false;
static u32 reply_source = 0;
static u16 reply_identifier = 0;
static u16 reply_sequence = 0;

void icmp_init(void) {
    next_identifier = 0x5A4F;
    next_sequence = 1;
    requests_sent = 0;
    replies_received = 0;
    requests_answered = 0;
    reply_waiting = false;
    reply_source = 0;
    reply_identifier = 0;
    reply_sequence = 0;
}

static int icmp_send_packet(u32 destination, u8 type, u16 identifier, u16 sequence,
                            const u8 *data, u32 length) {
    u8 packet[ICMP_HEADER_SIZE + 32];
    if (length > 32) {
        length = 32;
    }
    packet[0] = type;
    packet[1] = 0;
    packet[2] = 0;
    packet[3] = 0;
    packet[4] = (u8)(identifier >> 8);
    packet[5] = (u8)(identifier & 0xFF);
    packet[6] = (u8)(sequence >> 8);
    packet[7] = (u8)(sequence & 0xFF);
    if (data != NULL && length > 0) {
        memcpy(packet + ICMP_HEADER_SIZE, data, length);
    }
    u32 total = ICMP_HEADER_SIZE + length;
    u16 checksum = (u16)net_checksum(packet, total);
    packet[2] = (u8)(checksum >> 8);
    packet[3] = (u8)(checksum & 0xFF);
    return ipv4_send(destination, IPV4_PROTOCOL_ICMP, packet, total);
}

int icmp_send_echo_request(u32 destination, u16 identifier, u16 sequence) {
    static const u8 payload[8] = {'Z', '-', 'O', 'S', 'P', 'I', 'N', 'G'};
    if (icmp_send_packet(destination, ICMP_TYPE_ECHO_REQUEST, identifier, sequence,
                         payload, 8) != 0) {
        return -1;
    }
    requests_sent++;
    return 0;
}

int icmp_send_echo_reply(u32 destination, u16 identifier, u16 sequence,
                         const u8 *data, u32 length) {
    return icmp_send_packet(destination, ICMP_TYPE_ECHO_REPLY, identifier, sequence,
                            data, length);
}

void icmp_receive(const u8 *packet, u32 length, u32 source) {
    if (packet == NULL || length < ICMP_HEADER_SIZE) {
        return;
    }
    if (net_checksum(packet, length) != 0) {
        return;
    }
    u8 type = packet[0];
    u16 identifier = (u16)(((u16)packet[4] << 8) | packet[5]);
    u16 sequence = (u16)(((u16)packet[6] << 8) | packet[7]);
    if (type == ICMP_TYPE_ECHO_REQUEST) {
        icmp_send_echo_reply(source, identifier, sequence, packet + ICMP_HEADER_SIZE,
                             length - ICMP_HEADER_SIZE);
        requests_answered++;
        return;
    }
    if (type == ICMP_TYPE_ECHO_REPLY) {
        replies_received++;
        reply_waiting = true;
        reply_source = source;
        reply_identifier = identifier;
        reply_sequence = sequence;
    }
}

int icmp_ping(u32 destination, u32 timeout_ms, u32 *round_trip_ms) {
    if (!net_ready()) {
        return -1;
    }
    reply_waiting = false;
    u16 identifier = next_identifier++;
    u16 sequence = next_sequence++;
    if (icmp_send_echo_request(destination, identifier, sequence) != 0) {
        return -1;
    }
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        net_poll();
        if (reply_waiting && reply_source == destination &&
            reply_identifier == identifier && reply_sequence == sequence) {
            if (round_trip_ms != NULL) {
                *round_trip_ms = elapsed * 10;
            }
            return 0;
        }
        sched_sleep_ms(10);
    }
    return -1;
}

bool icmp_reply_waiting(void) {
    return reply_waiting;
}

void icmp_clear_reply(void) {
    reply_waiting = false;
}

u32 icmp_requests_sent(void) {
    return requests_sent;
}

u32 icmp_replies_received(void) {
    return replies_received;
}

u32 icmp_requests_answered(void) {
    return requests_answered;
}
