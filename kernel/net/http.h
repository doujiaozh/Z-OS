#ifndef Z_OS_HTTP_H
#define Z_OS_HTTP_H

#include "types.h"

#define HTTP_MAX_RESPONSE 4096
#define HTTP_DEFAULT_PORT 80
#define HTTP_TIMEOUT_MS 5000

struct http_response {
    u16 status;
    u32 header_length;
    u32 body_length;
    u32 declared_length;
    bool chunked;
    bool complete;
};

int http_parse_response(const char *data, u32 length, struct http_response *response);
int http_parse_url(const char *url, char *host, u32 host_capacity, u16 *port, char *path,
                   u32 path_capacity);
int http_get(u32 address, u16 port, const char *host, const char *path, char *body,
             u32 capacity, u32 timeout_ms);
int http_get_url(const char *url, char *body, u32 capacity, u32 timeout_ms);
u32 http_requests_sent(void);
u32 http_requests_completed(void);
u32 http_failures(void);

#endif
