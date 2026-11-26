#ifndef Z_OS_SOCKET_H
#define Z_OS_SOCKET_H

#include "types.h"

#define SOCKET_MAX 8
#define SOCKET_AF_INET 2
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

#endif
