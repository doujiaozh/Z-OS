#ifndef Z_OS_SOCKET_H
#define Z_OS_SOCKET_H

#include "types.h"

#define SOCKET_MAX 8
#define SOCKET_AF_INET 2
#define SOCKET_EAGAIN 11
#define SOCKET_EINVAL 22
#define SOCKET_POLLIN 0x0001
#define SOCKET_POLLOUT 0x0004
#define SOCKET_POLLERR 0x0008
#define SOCKET_SOCK_STREAM 1
#define SOCKET_SOCK_DGRAM 2

void socket_init(void);
int socket_create(u32 domain, u32 type, u32 protocol);
int socket_bind(int descriptor, u32 address, u16 port);
int socket_sendto(int descriptor, u32 address, u16 port, const void *data, u32 length);
int socket_recvfrom(int descriptor, void *buffer, u32 capacity, u32 timeout_ms,
                    u32 *source_ip, u16 *source_port);
int socket_close(int descriptor);
bool socket_valid(int descriptor);
u32 socket_port(int descriptor);
u32 socket_count(void);
u32 socket_bound_port(int descriptor);
int socket_connect(int descriptor, u32 address, u16 port, u32 timeout_ms);
int socket_accept(int descriptor, u32 timeout_ms);
int socket_send(int descriptor, const void *data, u32 length);
int socket_recv(int descriptor, void *buffer, u32 capacity, u32 timeout_ms);
int socket_create_for_owner(u32 owner, u32 domain, u32 type, u32 protocol);
u32 socket_owner(int descriptor);
void socket_cleanup_process(u32 owner);
u32 socket_owner_count(u32 owner);
int socket_set_nonblocking(int descriptor, bool enabled);
bool socket_is_nonblocking(int descriptor);
bool socket_read_ready(int descriptor);
bool socket_write_ready(int descriptor);
u32 socket_events(int descriptor);
u32 socket_poll_events(u32 fd, u32 events);
u32 socket_select(u32 read_mask, u32 write_mask, u32 timeout_ms,
                  u32 *ready_read, u32 *ready_write);

#endif
