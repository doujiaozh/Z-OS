#ifndef Z_OS_DHCP_H
#define Z_OS_DHCP_H

#include "types.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAX_MESSAGE 576
#define DHCP_FIXED_SIZE 236
#define DHCP_HEADER_SIZE (DHCP_FIXED_SIZE + 4)
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAMETER_LIST 55
#define DHCP_OPTION_END 255
#define DHCP_MESSAGE_DISCOVER 1
#define DHCP_MESSAGE_OFFER 2
#define DHCP_MESSAGE_REQUEST 3
#define DHCP_MESSAGE_ACK 5

struct dhcp_lease {
    u32 address;
    u32 server;
    u32 netmask;
    u32 gateway;
    u32 dns;
    u32 lease_seconds;
    bool valid;
};

int dhcp_discover(u32 timeout_ms);
const struct dhcp_lease *dhcp_current_lease(void);
u32 dhcp_build_discover(u8 *buffer, u32 capacity, u32 transaction);
u32 dhcp_build_request(u8 *buffer, u32 capacity, u32 transaction, u32 requested, u32 server);
bool dhcp_parse_offer(const u8 *message, u32 length, u32 transaction, struct dhcp_lease *lease,
                      u8 *message_type);
bool dhcp_apply_lease(const struct dhcp_lease *lease);
u32 dhcp_messages_sent(void);
u32 dhcp_messages_received(void);
u32 dhcp_failures(void);

#endif
