#ifndef Z_OS_USER_LIBC_H
#define Z_OS_USER_LIBC_H

#include "userlib.h"

typedef unsigned short u16;
typedef unsigned int u32;

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 1

struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    u32 sin_addr;
    u8 sin_zero[8];
};

static inline int net_socket(int domain, int type, int protocol) {
    (void)protocol;
    if (domain != AF_INET) {
        return -1;
    }
    return (int)zsys3(ZO_SYS_SOCKET, (long)domain, (long)type, 0);
}

static inline int net_bind(int descriptor, const struct sockaddr_in *address) {
    if (address == 0) {
        return -1;
    }
    return (int)zbind((long)descriptor, (long)address->sin_port);
}

static inline int net_connect(int descriptor, const struct sockaddr_in *address) {
    if (address == 0) {
        return -1;
    }
    return (int)zconnect((long)descriptor, (long)address->sin_addr, (long)address->sin_port);
}

static inline int net_send(int descriptor, const void *buffer, int length) {
    return (int)zsend_tcp((long)descriptor, buffer, (long)length);
}

static inline int net_recv(int descriptor, void *buffer, int capacity) {
    return (int)zrecv_tcp((long)descriptor, buffer, (long)capacity, 3000);
}

static inline int net_sendto(int descriptor, const void *buffer, int length,
                             const struct sockaddr_in *address) {
    if (address == 0) {
        return -1;
    }
    return (int)zsendto((long)descriptor, (long)address->sin_addr, (long)address->sin_port,
                        buffer, (long)length);
}

static inline int net_recvfrom(int descriptor, void *buffer, int capacity,
                               struct sockaddr_in *address) {
    (void)address;
    return (int)zrecvfrom((long)descriptor, buffer, (long)capacity, 3000);
}

static inline int net_select(int max_descriptor, unsigned long *read_mask,
                             unsigned long *write_mask, unsigned long timeout_ms) {
    (void)max_descriptor;
    unsigned long ready_read = 0;
    unsigned long ready_write = 0;
    int result = (int)zselect(*read_mask, *write_mask, timeout_ms, &ready_read, &ready_write);
    if (result >= 0) {
        *read_mask = ready_read;
        *write_mask = ready_write;
    }
    return result;
}

static inline int net_nonblocking(int descriptor, int enabled) {
    return (int)zset_nonblocking((long)descriptor, (long)enabled);
}

static inline int net_close(int descriptor) {
    return (int)zsocket_close((long)descriptor);
}

static inline u16 net_htons(u16 value) {
    return (u16)((value << 8) | (value >> 8));
}

#endif
