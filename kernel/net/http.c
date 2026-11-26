#include "http.h"
#include "dns.h"
#include "lib.h"
#include "net.h"
#include "socket.h"
#include "string.h"

static u32 requests_sent = 0;
static u32 requests_completed = 0;
static u32 failures = 0;

static int parse_status_line(const char *data, u32 length, struct http_response *response) {
    if (length < 12 || memcmp(data, "HTTP/", 5) != 0) {
        return -1;
    }
    u32 space = 5;
    while (space < length && data[space] != ' ') {
        space++;
    }
    if (space + 4 > length) {
        return -1;
    }
    u32 status = 0;
    for (u32 index = 0; index < 3; index++) {
        char digit = data[space + 1 + index];
        if (digit < '0' || digit > '9') {
            return -1;
        }
        status = status * 10 + (u32)(digit - '0');
    }
    response->status = (u16)status;
    return 0;
}

int http_parse_response(const char *data, u32 length, struct http_response *response) {
    if (data == NULL || response == NULL || length == 0) {
        return -1;
    }
    memset(response, 0, sizeof(struct http_response));
    if (parse_status_line(data, length, response) != 0) {
        return -1;
    }
    u32 position = 0;
    u32 body_start = 0;
    while (position + 3 < length) {
        if (data[position] == '\r' && data[position + 1] == '\n' &&
            data[position + 2] == '\r' && data[position + 3] == '\n') {
            body_start = position + 4;
            break;
        }
        position++;
    }
    if (body_start == 0) {
        return -1;
    }
    response->header_length = body_start;
    u32 line_start = 0;
    while (line_start < body_start) {
        u32 line_end = line_start;
        while (line_end + 1 < body_start &&
               !(data[line_end] == '\r' && data[line_end + 1] == '\n')) {
            line_end++;
        }
        u32 line_length = line_end - line_start;
        if (line_length >= 15 && memcmp(data + line_start, "Content-Length:", 15) == 0) {
            u32 value = 0;
            u32 index = line_start + 15;
            while (index < line_end && (data[index] == ' ' || data[index] == '\t')) {
                index++;
            }
            while (index < line_end && data[index] >= '0' && data[index] <= '9') {
                value = value * 10 + (u32)(data[index] - '0');
                index++;
            }
            response->declared_length = value;
        } else if (line_length >= 18 &&
                   memcmp(data + line_start, "Transfer-Encoding:", 18) == 0) {
            response->chunked = true;
        }
        line_start = line_end + 2;
    }
    response->body_length = length - body_start;
    if (!response->chunked && response->declared_length != 0) {
        response->complete = response->body_length >= response->declared_length;
        if (response->body_length > response->declared_length) {
            response->body_length = response->declared_length;
        }
    } else {
        response->complete = true;
    }
    return 0;
}

int http_parse_url(const char *url, char *host, u32 host_capacity, u16 *port, char *path,
                   u32 path_capacity) {
    if (url == NULL || host == NULL || path == NULL || host_capacity == 0 || path_capacity == 0) {
        return -1;
    }
    if (memcmp(url, "http:" "/" "/", 7) != 0) {
        return -1;
    }
    u32 position = 7;
    u32 host_length = 0;
    while (url[position] != 0 && url[position] != ':' && url[position] != '/') {
        if (host_length + 1 >= host_capacity) {
            return -1;
        }
        host[host_length++] = url[position++];
    }
    host[host_length] = 0;
    if (host_length == 0) {
        return -1;
    }
    u16 parsed_port = HTTP_DEFAULT_PORT;
    if (url[position] == ':') {
        position++;
        u32 value = 0;
        while (url[position] >= '0' && url[position] <= '9') {
            value = value * 10 + (u32)(url[position] - '0');
            position++;
        }
        if (value == 0 || value > 65535) {
            return -1;
        }
        parsed_port = (u16)value;
    }
    u32 path_length = 0;
    if (url[position] == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        if (url[position] != '/') {
            return -1;
        }
        while (url[position] != 0) {
            if (path_length + 1 >= path_capacity) {
                return -1;
            }
            path[path_length++] = url[position++];
        }
        path[path_length] = 0;
    }
    if (port != NULL) {
        *port = parsed_port;
    }
    return 0;
}

int http_get(u32 address, u16 port, const char *host, const char *path, char *body,
             u32 capacity, u32 timeout_ms) {
    if (address == 0 || port == 0 || host == NULL || path == NULL || body == NULL) {
        return -1;
    }
    int descriptor = socket_create(SOCKET_AF_INET, SOCKET_SOCK_STREAM, 0);
    if (descriptor < 0) {
        failures++;
        return -1;
    }
    if (socket_connect(descriptor, address, port, timeout_ms) != 0) {
        socket_close(descriptor);
        failures++;
        return -1;
    }
    char request[256];
    u32 request_length = 0;
    const char *prefix = "GET ";
    while (*prefix != 0 && request_length < sizeof(request) - 1) {
        request[request_length++] = *prefix++;
    }
    while (*path != 0 && request_length < sizeof(request) - 1) {
        request[request_length++] = *path++;
    }
    const char *suffix = " HTTP/1.0\r\nHost: ";
    while (*suffix != 0 && request_length < sizeof(request) - 1) {
        request[request_length++] = *suffix++;
    }
    while (*host != 0 && request_length < sizeof(request) - 1) {
        request[request_length++] = *host++;
    }
    const char *ending = "\r\nConnection: close\r\n\r\n";
    while (*ending != 0 && request_length < sizeof(request) - 1) {
        request[request_length++] = *ending++;
    }
    if (socket_send(descriptor, request, request_length) != (int)request_length) {
        socket_close(descriptor);
        failures++;
        return -1;
    }
    requests_sent++;
    static char response[HTTP_MAX_RESPONSE];
    u32 total = 0;
    while (total < sizeof(response) - 1) {
        int received = socket_recv(descriptor, response + total, (u32)(sizeof(response) - 1 - total),
                                   timeout_ms);
        if (received <= 0) {
            break;
        }
        total += (u32)received;
    }
    socket_close(descriptor);
    if (total == 0) {
        failures++;
        return -1;
    }
    response[total] = 0;
    struct http_response parsed;
    if (http_parse_response(response, total, &parsed) != 0) {
        failures++;
        return -1;
    }
    if (parsed.status < 200 || parsed.status >= 300) {
        failures++;
        return -1;
    }
    if (capacity == 0) {
        failures++;
        return -1;
    }
    u32 copy = parsed.body_length;
    if (copy > capacity - 1) {
        copy = capacity - 1;
    }
    memcpy(body, response + parsed.header_length, copy);
    body[copy] = 0;
    requests_completed++;
    return (int)copy;
}

int http_get_url(const char *url, char *body, u32 capacity, u32 timeout_ms) {
    char host[128];
    char path[128];
    u16 port = HTTP_DEFAULT_PORT;
    if (http_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        failures++;
        return -1;
    }
    u32 address = net_parse_ip(host);
    if (address == 0) {
        if (dns_resolve(host, &address, timeout_ms) != 0) {
            failures++;
            return -1;
        }
    }
    return http_get(address, port, host, path, body, capacity, timeout_ms);
}

u32 http_requests_sent(void) {
    return requests_sent;
}

u32 http_requests_completed(void) {
    return requests_completed;
}

u32 http_failures(void) {
    return failures;
}
