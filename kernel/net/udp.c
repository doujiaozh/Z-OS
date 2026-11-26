#include "udp.h"
#include "ipv4.h"
#include "lib.h"
#include "net.h"
#include "thread.h"
#include "string.h"

struct udp_queue_entry {
    u32 source_ip;
    u16 source_port;
    u16 length;
    u8 data[UDP_MAX_PAYLOAD];
};

struct udp_socket_record {
    bool used;
    u16 port;
    u32 received;
    u32 sent;
    u32 head;
    u32 tail;
    u32 count;
    struct udp_queue_entry queue[UDP_QUEUE_DEPTH];
};

static struct udp_socket_record sockets[UDP_MAX_SOCKETS];
static u32 datagrams_sent = 0;
static u32 datagrams_received = 0;
static u32 datagrams_dropped = 0;

void udp_init(void) {
    memset(sockets, 0, sizeof(sockets));
    datagrams_sent = 0;
    datagrams_received = 0;
    datagrams_dropped = 0;
}

static int find_socket_by_port(u16 port) {
    for (u32 index = 0; index < UDP_MAX_SOCKETS; index++) {
        if (sockets[index].used && sockets[index].port == port) {
            return (int)index;
        }
    }
    return -1;
}

int udp_bind(u16 port) {
    if (port == 0) {
        return -1;
    }
    if (find_socket_by_port(port) >= 0) {
        return -1;
    }
    for (u32 index = 0; index < UDP_MAX_SOCKETS; index++) {
        if (!sockets[index].used) {
            memset(&sockets[index], 0, sizeof(struct udp_socket_record));
            sockets[index].used = true;
            sockets[index].port = port;
            return (int)index;
        }
    }
    return -1;
}

int udp_unbind(u16 port) {
    int index = find_socket_by_port(port);
    if (index < 0) {
        return -1;
    }
    memset(&sockets[index], 0, sizeof(struct udp_socket_record));
    return 0;
}

bool udp_socket_used(u32 index) {
    return index < UDP_MAX_SOCKETS && sockets[index].used;
}

u16 udp_socket_port(u32 index) {
    return index < UDP_MAX_SOCKETS ? sockets[index].port : 0;
}

u32 udp_socket_received(u32 index) {
    return index < UDP_MAX_SOCKETS ? sockets[index].received : 0;
}

u32 udp_socket_sent(u32 index) {
    return index < UDP_MAX_SOCKETS ? sockets[index].sent : 0;
}

u32 udp_socket_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < UDP_MAX_SOCKETS; index++) {
        if (sockets[index].used) {
            count++;
        }
    }
    return count;
}

struct udp_socket_state udp_socket_info(u32 index) {
    struct udp_socket_state info;
    memset(&info, 0, sizeof(info));
    if (index < UDP_MAX_SOCKETS && sockets[index].used) {
        info.used = true;
        info.port = sockets[index].port;
        info.received = sockets[index].received;
        info.sent = sockets[index].sent;
        info.queued = sockets[index].count;
    }
    return info;
}

static void udp_enqueue(u32 index, u32 source_ip, u16 source_port,
                        const void *data, u32 length) {
    if (index >= UDP_MAX_SOCKETS || !sockets[index].used) {
        datagrams_dropped++;
        return;
    }
    if (length > UDP_MAX_PAYLOAD) {
        length = UDP_MAX_PAYLOAD;
    }
    if (sockets[index].count >= UDP_QUEUE_DEPTH) {
        sockets[index].tail = (sockets[index].tail + 1) % UDP_QUEUE_DEPTH;
        sockets[index].count--;
    }
    struct udp_queue_entry *entry = &sockets[index].queue[sockets[index].head];
    entry->source_ip = source_ip;
    entry->source_port = source_port;
    entry->length = (u16)length;
    if (data != NULL && length > 0) {
        memcpy(entry->data, data, length);
    }
    sockets[index].head = (sockets[index].head + 1) % UDP_QUEUE_DEPTH;
    sockets[index].count++;
    sockets[index].received++;
    datagrams_received++;
}

int udp_sendto(u32 index, u32 destination_ip, u16 destination_port,
               const void *data, u32 length) {
    if (index >= UDP_MAX_SOCKETS || !sockets[index].used) {
        return -1;
    }
    if (length > UDP_MAX_PAYLOAD) {
        return -1;
    }
    u8 packet[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
    u16 source_port = sockets[index].port;
    u16 total = (u16)(UDP_HEADER_SIZE + length);
    packet[0] = (u8)(source_port >> 8);
    packet[1] = (u8)(source_port & 0xFF);
    packet[2] = (u8)(destination_port >> 8);
    packet[3] = (u8)(destination_port & 0xFF);
    packet[4] = (u8)(total >> 8);
    packet[5] = (u8)(total & 0xFF);
    packet[6] = 0;
    packet[7] = 0;
    if (data != NULL && length > 0) {
        memcpy(packet + UDP_HEADER_SIZE, data, length);
    }
    if (ipv4_send(destination_ip, IPV4_PROTOCOL_UDP, packet, total) != 0) {
        return -1;
    }
    sockets[index].sent++;
    datagrams_sent++;
    return 0;
}

void udp_receive(const u8 *packet, u32 length, u32 source) {
    if (packet == NULL || length < UDP_HEADER_SIZE) {
        return;
    }
    u16 destination_port = (u16)(((u16)packet[2] << 8) | packet[3]);
    u16 payload_length = (u16)(((u16)packet[4] << 8) | packet[5]);
    if (payload_length < UDP_HEADER_SIZE || payload_length > length) {
        datagrams_dropped++;
        return;
    }
    u16 source_port = (u16)(((u16)packet[0] << 8) | packet[1]);
    int index = find_socket_by_port(destination_port);
    if (index < 0) {
        datagrams_dropped++;
        return;
    }
    udp_enqueue((u32)index, source, source_port, packet + UDP_HEADER_SIZE,
                payload_length - UDP_HEADER_SIZE);
}

int udp_recv(u32 index, void *buffer, u32 capacity, u32 timeout_ms,
             u32 *source_ip, u16 *source_port) {
    if (index >= UDP_MAX_SOCKETS || !sockets[index].used || buffer == NULL) {
        return -1;
    }
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        if (sockets[index].count > 0) {
            struct udp_queue_entry *entry = &sockets[index].queue[sockets[index].tail];
            u32 length = entry->length;
            if (length > capacity) {
                length = capacity;
            }
            memcpy(buffer, entry->data, length);
            if (source_ip != NULL) {
                *source_ip = entry->source_ip;
            }
            if (source_port != NULL) {
                *source_port = entry->source_port;
            }
            sockets[index].tail = (sockets[index].tail + 1) % UDP_QUEUE_DEPTH;
            sockets[index].count--;
            return (int)length;
        }
        net_poll();
        net_delay_ms(10);
    }
    return -1;
}

int udp_inject_datagram(u16 port, u32 source_ip, u16 source_port,
                        const void *data, u32 length) {
    int index = find_socket_by_port(port);
    if (index < 0) {
        return -1;
    }
    udp_enqueue((u32)index, source_ip, source_port, data, length);
    return 0;
}

u32 udp_datagrams_sent(void) {
    return datagrams_sent;
}

u32 udp_datagrams_received(void) {
    return datagrams_received;
}

u32 udp_datagrams_dropped(void) {
    return datagrams_dropped;
}
