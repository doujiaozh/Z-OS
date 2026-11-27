#include "libc.h"

#define MINIDB_HOST_ADDRESS ((10UL << 24) | (0UL << 16) | (2UL << 8) | 2UL)
#define MINIDB_HOST_PORT 6379
#define DB_BUFFER_SIZE 1024

static const char DB_REQUEST[] = "*2\r\n$3\r\nGET\r\n$14\r\nzos:minidb:key\r\n";

static int parse_bulk(const char *data, int length, int *value_offset, int *value_length) {
    if (length < 2) {
        return 0;
    }
    if (data[0] != '$') {
        return -1;
    }
    int index = 1;
    int negative = 0;
    if (data[index] == '-') {
        negative = 1;
        index++;
    }
    int declared = 0;
    int digits = 0;
    while (index < length) {
        char current = data[index];
        if (current == '\r') {
            break;
        }
        if (current < '0' || current > '9') {
            return -1;
        }
        if (declared > 100000) {
            return -1;
        }
        declared = declared * 10 + (current - '0');
        digits++;
        index++;
    }
    if (digits == 0) {
        return -1;
    }
    if (index + 1 >= length) {
        return 0;
    }
    if (data[index] != '\r' || data[index + 1] != '\n') {
        return -1;
    }
    if (negative) {
        return 2;
    }
    int body = index + 2;
    if (body + declared + 2 > length) {
        return 0;
    }
    if (data[body + declared] != '\r' || data[body + declared + 1] != '\n') {
        return -1;
    }
    *value_offset = body;
    *value_length = declared;
    return 1;
}

__attribute__((noreturn)) void _start(void) {
    int descriptor = net_socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor <= 0) {
        zwrite("dbclient: socket failed\n");
        zexit(1);
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = (u16)MINIDB_HOST_PORT;
    address.sin_addr = (u32)MINIDB_HOST_ADDRESS;
    for (int index = 0; index < 8; index++) {
        address.sin_zero[index] = 0;
    }
    zwrite("dbclient: connecting to 10.0.2.2:6379\n");
    if (net_connect(descriptor, &address) != 0) {
        zwrite("dbclient: connect failed\n");
        net_close(descriptor);
        zexit(1);
    }
    zwrite("dbclient: connected\n");
    int request_length = (int)zstrlen(DB_REQUEST);
    if (net_send(descriptor, DB_REQUEST, request_length) != request_length) {
        zwrite("dbclient: send failed\n");
        net_close(descriptor);
        zexit(1);
    }
    static char response[DB_BUFFER_SIZE];
    int total = 0;
    int value_offset = 0;
    int value_length = 0;
    for (;;) {
        int status = parse_bulk(response, total, &value_offset, &value_length);
        if (status == 1) {
            break;
        }
        if (status == 2) {
            net_close(descriptor);
            zwrite("dbclient: value (nil)\n");
            zexit(0);
        }
        if (status < 0) {
            zwrite("dbclient: invalid response\n");
            net_close(descriptor);
            zexit(1);
        }
        if (total >= DB_BUFFER_SIZE - 1) {
            zwrite("dbclient: response too large\n");
            net_close(descriptor);
            zexit(1);
        }
        int received = net_recv(descriptor, response + total, DB_BUFFER_SIZE - 1 - total);
        if (received <= 0) {
            zwrite("dbclient: receive failed\n");
            net_close(descriptor);
            zexit(1);
        }
        total += received;
    }
    net_close(descriptor);
    zwrite("dbclient: value ");
    if (value_length > 0) {
        if (zwrite_fd(1, response + value_offset, value_length) != value_length) {
            zwrite("dbclient: output failed\n");
            zexit(1);
        }
    }
    zwrite("\n");
    zexit(0);
}
