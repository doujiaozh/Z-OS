#include "socket.h"
#include "lib.h"
#include "net.h"
#include "string.h"
#include "tcp.h"
#include "udp.h"

struct socket_record {
    bool used;
    u32 domain;
    u32 type;
    int udp_index;
    int tcp_index;
    u32 bound_port;
    u32 remote_address;
    u16 remote_port;
};

static struct socket_record table[SOCKET_MAX];

void socket_init(void) {
    memset(table, 0, sizeof(table));
    for (u32 index = 0; index < SOCKET_MAX; index++) {
        table[index].udp_index = -1;
        table[index].tcp_index = -1;
    }
}

bool socket_valid(int descriptor) {
    if (descriptor < 1 || descriptor > SOCKET_MAX) {
        return false;
    }
    return table[descriptor - 1].used;
}

int socket_create(u32 domain, u32 type, u32 protocol) {
    if (domain != SOCKET_AF_INET || (type != SOCKET_SOCK_DGRAM && type != SOCKET_SOCK_STREAM)) {
        return -1;
    }
    (void)protocol;
    for (u32 index = 0; index < SOCKET_MAX; index++) {
        if (!table[index].used) {
            memset(&table[index], 0, sizeof(struct socket_record));
            table[index].used = true;
            table[index].domain = domain;
            table[index].type = type;
            table[index].udp_index = -1;
            table[index].tcp_index = -1;
            return (int)(index + 1);
        }
    }
    return -1;
}

int socket_bind(int descriptor, u32 address, u16 port) {
    if (!socket_valid(descriptor) || port == 0) {
        return -1;
    }
    (void)address;
    struct socket_record *record = &table[descriptor - 1];
    if (record->type == SOCKET_SOCK_STREAM) {
        int tcp_index = tcp_listen(port);
        if (tcp_index < 0) {
            return -1;
        }
        record->tcp_index = tcp_index;
        record->bound_port = port;
        return 0;
    }
    int udp_index = udp_bind(port);
    if (udp_index < 0) {
        return -1;
    }
    record->udp_index = udp_index;
    record->bound_port = port;
    return 0;
}

int socket_sendto(int descriptor, u32 address, u16 port, const void *data, u32 length) {
    if (!socket_valid(descriptor)) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->type == SOCKET_SOCK_STREAM) {
        if (record->tcp_index < 0) {
            int tcp_index = tcp_connect(address, port, 3000);
            if (tcp_index < 0) {
                return -1;
            }
            record->tcp_index = tcp_index;
            record->remote_address = address;
            record->remote_port = port;
        }
        return tcp_send(record->tcp_index, data, length);
    }
    if (record->udp_index < 0) {
        int udp_index = udp_bind((u16)(0x4000 + descriptor));
        if (udp_index < 0) {
            return -1;
        }
        record->udp_index = udp_index;
        record->bound_port = udp_socket_port((u32)udp_index);
    }
    if (udp_sendto((u32)record->udp_index, address, port, data, length) != 0) {
        return -1;
    }
    return (int)length;
}

int socket_recvfrom(int descriptor, void *buffer, u32 capacity, u32 timeout_ms,
                    u32 *source_ip, u16 *source_port) {
    if (!socket_valid(descriptor) || buffer == NULL) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->type == SOCKET_SOCK_STREAM) {
        if (record->tcp_index < 0) {
            return -1;
        }
        if (source_ip != NULL) {
            *source_ip = record->remote_address;
        }
        if (source_port != NULL) {
            *source_port = record->remote_port;
        }
        return tcp_recv(record->tcp_index, buffer, capacity, timeout_ms);
    }
    if (record->udp_index < 0) {
        return -1;
    }
    return udp_recv((u32)record->udp_index, buffer, capacity, timeout_ms, source_ip, source_port);
}

int socket_close(int descriptor) {
    if (!socket_valid(descriptor)) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->udp_index >= 0) {
        udp_unbind(udp_socket_port((u32)record->udp_index));
    }
    if (record->tcp_index >= 0) {
        tcp_close(record->tcp_index);
    }
    memset(record, 0, sizeof(struct socket_record));
    record->udp_index = -1;
    return 0;
}

u32 socket_port(int descriptor) {
    if (!socket_valid(descriptor)) {
        return 0;
    }
    return table[descriptor - 1].bound_port;
}

u32 socket_bound_port(int descriptor) {
    if (!socket_valid(descriptor)) {
        return 0;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->udp_index < 0) {
        return 0;
    }
    return udp_socket_port((u32)record->udp_index);
}

u32 socket_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < SOCKET_MAX; index++) {
        if (table[index].used) {
            count++;
        }
    }
    return count;
}

int socket_connect(int descriptor, u32 address, u16 port, u32 timeout_ms) {
    if (!socket_valid(descriptor) || address == 0 || port == 0) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->type != SOCKET_SOCK_STREAM) {
        return -1;
    }
    int tcp_index = tcp_connect(address, port, timeout_ms);
    if (tcp_index < 0) {
        return -1;
    }
    record->tcp_index = tcp_index;
    record->remote_address = address;
    record->remote_port = port;
    return 0;
}

int socket_accept(int descriptor, u32 timeout_ms) {
    if (!socket_valid(descriptor)) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->type != SOCKET_SOCK_STREAM || record->tcp_index < 0) {
        return -1;
    }
    int accepted = tcp_accept(record->tcp_index, timeout_ms);
    if (accepted < 0) {
        return -1;
    }
    for (u32 index = 0; index < SOCKET_MAX; index++) {
        if (!table[index].used) {
            memset(&table[index], 0, sizeof(struct socket_record));
            table[index].used = true;
            table[index].domain = SOCKET_AF_INET;
            table[index].type = SOCKET_SOCK_STREAM;
            table[index].udp_index = -1;
            table[index].tcp_index = accepted;
            table[index].bound_port = socket_bound_port(descriptor);
            table[index].remote_address = tcp_remote_address(accepted);
            table[index].remote_port = (u16)tcp_remote_port(accepted);
            return (int)(index + 1);
        }
    }
    return -1;
}

int socket_send(int descriptor, const void *data, u32 length) {
    if (!socket_valid(descriptor) || data == NULL || length == 0) {
        return -1;
    }
    struct socket_record *record = &table[descriptor - 1];
    if (record->type != SOCKET_SOCK_STREAM || record->tcp_index < 0) {
        return -1;
    }
    return tcp_send(record->tcp_index, data, length);
}

int socket_recv(int descriptor, void *buffer, u32 capacity, u32 timeout_ms) {
    return socket_recvfrom(descriptor, buffer, capacity, timeout_ms, NULL, NULL);
}
