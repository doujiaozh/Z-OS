#include "userlib.h"

#define USER_TCP_PORT 34567
#define USER_TCP_REMOTE ((10UL << 24) | (0UL << 16) | (2UL << 8) | 2UL)
#define USER_MARK_START 0x50
#define USER_MARK_CONNECTED 0x51
#define USER_MARK_SENT 0x52
#define USER_MARK_ECHO 0x53
#define USER_MARK_LEAK 0x54
#define USER_MARK_FAILURE 0x5F

__attribute__((noreturn)) void _start(void) {
    zmark(USER_MARK_START);
    long descriptor = zsocket_stream();
    if (descriptor <= 0) {
        zmark(USER_MARK_FAILURE);
        zwrite("nettest2: socket failed\n");
        zexit(1);
    }
    if (zconnect(descriptor, USER_TCP_REMOTE, USER_TCP_PORT) != 0) {
        zmark(USER_MARK_FAILURE);
        zwrite("nettest2: connect failed\n");
        zsocket_close(descriptor);
        zexit(1);
    }
    zmark(USER_MARK_CONNECTED);
    zwrite("nettest2: connected\n");
    const char *payload = "ZOS-USER-TCP-PAYLOAD";
    long length = zstrlen(payload);
    if (zsend_tcp(descriptor, payload, length) != length) {
        zmark(USER_MARK_FAILURE);
        zwrite("nettest2: send failed\n");
        zsocket_close(descriptor);
        zexit(1);
    }
    zmark(USER_MARK_SENT);
    char reply[128];
    long received = zrecv_tcp(descriptor, reply, sizeof(reply) - 1, 3000);
    if (received != length) {
        zmark(USER_MARK_FAILURE);
        zwrite("nettest2: receive failed\n");
        zsocket_close(descriptor);
        zexit(1);
    }
    reply[received] = 0;
    if (zmemeq(reply, payload, (u64)length) == 0) {
        zmark(USER_MARK_FAILURE);
        zwrite("nettest2: payload mismatch\n");
        zsocket_close(descriptor);
        zexit(1);
    }
    zmark(USER_MARK_ECHO);
    zwrite("nettest2: tcp echo verified\n");
    zsocket_close(descriptor);
    long leak_listener = zsocket_stream();
    if (leak_listener > 0) {
        zbind(leak_listener, 34568);
    }
    long leak_datagram = zsocket();
    if (leak_datagram > 0) {
        zbind(leak_datagram, 34569);
    }
    zmark(USER_MARK_LEAK);
    zwrite("nettest2: leaving sockets open for cleanup\n");
    zexit(0);
    for (;;) {
    }
}
