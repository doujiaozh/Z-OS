#include "libc.h"

#define WGET_MARK_START 0x60
#define WGET_MARK_CONNECTED 0x61
#define WGET_MARK_RECEIVED 0x62
#define WGET_MARK_SAVED 0x63
#define WGET_MARK_FAILURE 0x6F
#define WGET_BUFFER_SIZE 4096

static int find_header_end(const char *data, int length) {
    for (int index = 0; index + 3 < length; index++) {
        if (data[index] == '\r' && data[index + 1] == '\n' &&
            data[index + 2] == '\r' && data[index + 3] == '\n') {
            return index + 4;
        }
    }
    return -1;
}

__attribute__((noreturn)) void _start(void) {
    zmark(WGET_MARK_START);
    long configured_port = zgettestport();
    unsigned long port = configured_port > 0 ? (unsigned long)configured_port : 80;
    int descriptor = net_socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor <= 0) {
        zmark(WGET_MARK_FAILURE);
        zwrite("wget: socket failed\n");
        zexit(1);
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = (u16)port;
    address.sin_addr = (10UL << 24) | (0UL << 16) | (2UL << 8) | 2UL;
    for (int index = 0; index < 8; index++) {
        address.sin_zero[index] = 0;
    }
    if (net_connect(descriptor, &address) != 0) {
        zmark(WGET_MARK_FAILURE);
        zwrite("wget: connect failed\n");
        net_close(descriptor);
        zexit(1);
    }
    zmark(WGET_MARK_CONNECTED);
    zwrite("wget: connected\n");
    const char *request = "GET /index.html HTTP/1.0\r\nHost: z-os\r\nConnection: close\r\n\r\n";
    long request_length = zstrlen(request);
    if (net_send(descriptor, request, (int)request_length) != (int)request_length) {
        zmark(WGET_MARK_FAILURE);
        zwrite("wget: request failed\n");
        net_close(descriptor);
        zexit(1);
    }
    static char buffer[WGET_BUFFER_SIZE];
    int total = 0;
    while (total < WGET_BUFFER_SIZE - 1) {
        int chunk = WGET_BUFFER_SIZE - 1 - total;
        if (chunk > 1500) {
            chunk = 1500;
        }
        int received = net_recv(descriptor, buffer + total, chunk);
        if (received <= 0) {
            break;
        }
        total += received;
    }
    net_close(descriptor);
    buffer[total] = 0;
    int header_end = find_header_end(buffer, total);
    if (total <= 0 || header_end < 0) {
        zmark(WGET_MARK_FAILURE);
        zwrite("wget: invalid response\n");
        zexit(1);
    }
    zmark(WGET_MARK_RECEIVED);
    int body_length = total - header_end;
    zwrite("wget: received body\n");
    long output = zopen("/wget.out", ZO_OPEN_WRITE);
    if (output < 0) {
        zmark(WGET_MARK_FAILURE);
        zwrite("wget: open failed\n");
        zexit(1);
    }
    if (body_length > 0) {
        if (zwrite_fd(output, buffer + header_end, body_length) != body_length) {
            zmark(WGET_MARK_FAILURE);
            zwrite("wget: write failed\n");
            zclose(output);
            zexit(1);
        }
    }
    zclose(output);
    zmark(WGET_MARK_SAVED);
    zwrite("wget: saved body\n");
    zexit(0);
    for (;;) {
    }
}
