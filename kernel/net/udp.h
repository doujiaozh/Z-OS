#ifndef Z_OS_UDP_H
#define Z_OS_UDP_H

#include "types.h"

#define UDP_MAX_SOCKETS 8
#define UDP_QUEUE_DEPTH 4
#define UDP_MAX_PAYLOAD 512
#define UDP_HEADER_SIZE 8

struct udp_socket_state {
    bool used;
    u16 port;
    u32 received;
    u32 sent;
    u32 queued;
};

void udp_init(void);
int udp_bind(u16 port);
int udp_unbind(u16 port);
bool udp_socket_used(u32 index);
u16 udp_socket_port(u32 index);
u32 udp_socket_received(u32 index);
u32 udp_socket_sent(u32 index);
u32 udp_socket_count(void);
int udp_sendto(u32 index, u32 destination_ip, u16 destination_port,
               const void *data, u32 length);
void udp_receive(const u8 *packet, u32 length, u32 source);
int udp_recv(u32 index, void *buffer, u32 capacity, u32 timeout_ms,
             u32 *source_ip, u16 *source_port);
int udp_inject_datagram(u16 port, u32 source_ip, u16 source_port,
                        const void *data, u32 length);
u32 udp_datagrams_sent(void);
u32 udp_datagrams_received(void);
u32 udp_datagrams_dropped(void);
struct udp_socket_state udp_socket_info(u32 index);
u32 udp_socket_queued(u32 index);

#endif
