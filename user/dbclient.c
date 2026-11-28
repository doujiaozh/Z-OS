#include "libc.h"

#define DBCLIENT_REMOTE ((10UL << 24) | (0UL << 16) | (2UL << 8) | 2UL)
#define DBCLIENT_DEFAULT_PORT 6379
#define DBCLIENT_HOST_KEY "zos:minidb:key"
#define DBCLIENT_WRITE_KEY "zos:minidb:write"
#define DBCLIENT_WRITE_VALUE "zos-dbclient-write-ok"
#define DBCLIENT_COMMAND_SIZE 512
#define DBCLIENT_RESPONSE_SIZE 2048
#define DBCLIENT_VALUE_SIZE 1024
#define DBCLIENT_MARK_START 0x70
#define DBCLIENT_MARK_CONNECTED 0x71
#define DBCLIENT_MARK_SET 0x72
#define DBCLIENT_MARK_GET 0x73
#define DBCLIENT_MARK_FAILURE 0x7F

struct db_reader {
    char buffer[DBCLIENT_RESPONSE_SIZE];
    int length;
    int position;
};

static struct db_reader response_reader;
static char command_buffer[DBCLIENT_COMMAND_SIZE];
static char reply_line[64];
static char host_value[DBCLIENT_VALUE_SIZE];
static char write_value[DBCLIENT_VALUE_SIZE];

static int text_length(const char *text) {
    return (int)zstrlen(text);
}

static int append_char(char *buffer, int capacity, int position, char value) {
    if (position < 0 || position >= capacity) {
        return -1;
    }
    buffer[position] = value;
    return position + 1;
}

static int append_text(char *buffer, int capacity, int position, const char *text) {
    int length = text_length(text);
    if (position < 0 || position + length > capacity) {
        return -1;
    }
    for (int index = 0; index < length; index++) {
        buffer[position + index] = text[index];
    }
    return position + length;
}

static int append_uint(char *buffer, int capacity, int position, unsigned long value) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0);
    if (position < 0 || position + count > capacity) {
        return -1;
    }
    for (int index = count - 1; index >= 0; index--) {
        buffer[position++] = digits[index];
    }
    return position;
}

static int send_command(int descriptor, const char *parts[], int count) {
    char *buffer = command_buffer;
    int position = 0;
    position = append_char(buffer, DBCLIENT_COMMAND_SIZE, position, '*');
    if (position < 0) {
        return -1;
    }
    position = append_uint(buffer, DBCLIENT_COMMAND_SIZE, position, (unsigned long)count);
    if (position < 0) {
        return -1;
    }
    position = append_text(buffer, DBCLIENT_COMMAND_SIZE, position, "\r\n");
    if (position < 0) {
        return -1;
    }
    for (int index = 0; index < count; index++) {
        position = append_char(buffer, DBCLIENT_COMMAND_SIZE, position, '$');
        if (position < 0) {
            return -1;
        }
        position = append_uint(buffer, DBCLIENT_COMMAND_SIZE, position,
                               (unsigned long)text_length(parts[index]));
        if (position < 0) {
            return -1;
        }
        position = append_text(buffer, DBCLIENT_COMMAND_SIZE, position, "\r\n");
        if (position < 0) {
            return -1;
        }
        position = append_text(buffer, DBCLIENT_COMMAND_SIZE, position, parts[index]);
        if (position < 0) {
            return -1;
        }
        position = append_text(buffer, DBCLIENT_COMMAND_SIZE, position, "\r\n");
        if (position < 0) {
            return -1;
        }
    }
    int sent = net_send(descriptor, buffer, position);
    return sent == position ? 0 : -1;
}

static int reader_fill(int descriptor, struct db_reader *reader) {
    if (reader->position > 0) {
        int remaining = reader->length - reader->position;
        for (int index = 0; index < remaining; index++) {
            reader->buffer[index] = reader->buffer[reader->position + index];
        }
        reader->length = remaining;
        reader->position = 0;
    }
    if (reader->length >= DBCLIENT_RESPONSE_SIZE) {
        return -1;
    }
    int received = net_recv(descriptor, reader->buffer + reader->length,
                            DBCLIENT_RESPONSE_SIZE - reader->length);
    if (received <= 0) {
        return -1;
    }
    reader->length += received;
    return 0;
}

static int reader_line(int descriptor, struct db_reader *reader, char *line, int capacity) {
    for (;;) {
        for (int index = reader->position; index + 1 < reader->length; index++) {
            if (reader->buffer[index] == '\r' && reader->buffer[index + 1] == '\n') {
                int count = index - reader->position;
                if (count >= capacity) {
                    return -1;
                }
                for (int offset = 0; offset < count; offset++) {
                    line[offset] = reader->buffer[reader->position + offset];
                }
                line[count] = 0;
                reader->position = index + 2;
                return count;
            }
        }
        if (reader_fill(descriptor, reader) != 0) {
            return -1;
        }
    }
}

static int reader_exact(int descriptor, struct db_reader *reader, char *output, int count) {
    while (reader->length - reader->position < count) {
        if (reader_fill(descriptor, reader) != 0) {
            return -1;
        }
    }
    for (int index = 0; index < count; index++) {
        output[index] = reader->buffer[reader->position + index];
    }
    reader->position += count;
    return 0;
}

static int parse_number(const char *text) {
    if (text[0] == 0) {
        return -1;
    }
    int value = 0;
    for (int index = 0; text[index] != 0; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + (text[index] - '0');
        if (value > 1048576) {
            return -1;
        }
    }
    return value;
}

static int read_ok(int descriptor, struct db_reader *reader) {
    if (reader_line(descriptor, reader, reply_line, (int)sizeof(reply_line)) < 0) {
        return -1;
    }
    if (reply_line[0] != '+' || reply_line[1] != 'O' || reply_line[2] != 'K' ||
        reply_line[3] != 0) {
        return -1;
    }
    return 0;
}

static int read_bulk(int descriptor, struct db_reader *reader, char *value, int capacity,
                     int *value_length) {
    if (reader_line(descriptor, reader, reply_line, (int)sizeof(reply_line)) < 0) {
        return -1;
    }
    if (reply_line[0] != '$') {
        return -1;
    }
    int length = parse_number(reply_line + 1);
    if (length < 0) {
        value[0] = 0;
        *value_length = 0;
        return 1;
    }
    if (length >= capacity) {
        return -1;
    }
    if (reader_exact(descriptor, reader, value, length) != 0) {
        return -1;
    }
    char terminator[2];
    if (reader_exact(descriptor, reader, terminator, 2) != 0) {
        return -1;
    }
    if (terminator[0] != '\r' || terminator[1] != '\n') {
        return -1;
    }
    value[length] = 0;
    *value_length = length;
    return 0;
}

static __attribute__((noreturn)) void dbclient_fail(const char *message) {
    zmark(DBCLIENT_MARK_FAILURE);
    zwrite("dbclient: ");
    zwrite(message);
    zwrite("\n");
    zexit(1);
}

__attribute__((noreturn)) void _start(void) {
    zmark(DBCLIENT_MARK_START);
    long configured_port = zgettestport();
    unsigned long port = configured_port > 0 && configured_port < 65535
                             ? (unsigned long)configured_port
                             : (unsigned long)DBCLIENT_DEFAULT_PORT;
    int descriptor = net_socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor <= 0) {
        dbclient_fail("socket failed");
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = (u16)port;
    address.sin_addr = DBCLIENT_REMOTE;
    for (int index = 0; index < 8; index++) {
        address.sin_zero[index] = 0;
    }
    if (net_connect(descriptor, &address) != 0) {
        net_close(descriptor);
        dbclient_fail("connect failed");
    }
    zmark(DBCLIENT_MARK_CONNECTED);
    zwrite("dbclient: connected\n");

    response_reader.length = 0;
    response_reader.position = 0;

    const char *set_parts[3];
    set_parts[0] = "SET";
    set_parts[1] = DBCLIENT_WRITE_KEY;
    set_parts[2] = DBCLIENT_WRITE_VALUE;
    if (send_command(descriptor, set_parts, 3) != 0) {
        net_close(descriptor);
        dbclient_fail("set send failed");
    }
    if (read_ok(descriptor, &response_reader) != 0) {
        net_close(descriptor);
        dbclient_fail("set reply failed");
    }
    zmark(DBCLIENT_MARK_SET);
    zwrite("dbclient: set ok\n");

    const char *get_host[2];
    get_host[0] = "GET";
    get_host[1] = DBCLIENT_HOST_KEY;
    if (send_command(descriptor, get_host, 2) != 0) {
        net_close(descriptor);
        dbclient_fail("host get send failed");
    }
    int host_length = 0;
    if (read_bulk(descriptor, &response_reader, host_value, DBCLIENT_VALUE_SIZE, &host_length) != 0) {
        net_close(descriptor);
        dbclient_fail("host value missing");
    }
    zwrite("dbclient: value ");
    zwrite(host_value);
    zwrite("\n");

    const char *get_write[2];
    get_write[0] = "GET";
    get_write[1] = DBCLIENT_WRITE_KEY;
    if (send_command(descriptor, get_write, 2) != 0) {
        net_close(descriptor);
        dbclient_fail("write get send failed");
    }
    int write_length = 0;
    if (read_bulk(descriptor, &response_reader, write_value, DBCLIENT_VALUE_SIZE,
                  &write_length) != 0) {
        net_close(descriptor);
        dbclient_fail("write value missing");
    }
    if (write_length != text_length(DBCLIENT_WRITE_VALUE) ||
        zmemeq(write_value, DBCLIENT_WRITE_VALUE, (unsigned long)write_length) == 0) {
        net_close(descriptor);
        dbclient_fail("roundtrip mismatch");
    }
    zwrite("dbclient: roundtrip ");
    zwrite(write_value);
    zwrite("\n");
    zmark(DBCLIENT_MARK_GET);

    net_close(descriptor);
    zwrite("dbclient: done\n");
    zexit(0);
}
