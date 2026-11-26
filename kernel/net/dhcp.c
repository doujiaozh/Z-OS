#include "dhcp.h"
#include "arp.h"
#include "eth.h"
#include "ipv4.h"
#include "lib.h"
#include "net.h"
#include "string.h"
#include "udp.h"

#define DHCP_BROADCAST_IP 0xFFFFFFFFU

static struct dhcp_lease current_lease;
static u32 messages_sent = 0;
static u32 messages_received = 0;
static u32 failures = 0;
static u32 transaction_counter = 0x3A5F0000U;

static void write_be16(u8 *data, u16 value) {
    data[0] = (u8)(value >> 8);
    data[1] = (u8)(value & 0xFF);
}

static void write_be32(u8 *data, u32 value) {
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)(value & 0xFF);
}

static u32 read_be32(const u8 *data) {
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static u32 append_option(u8 *buffer, u32 position, u8 option, u8 length, const u8 *value) {
    buffer[position++] = option;
    buffer[position++] = length;
    if (value != NULL && length > 0) {
        memcpy(buffer + position, value, length);
        position += length;
    }
    return position;
}

static u32 build_message(u8 *buffer, u32 capacity, u32 transaction, u8 message_type,
                         u32 requested, u32 server) {
    if (buffer == NULL || capacity < DHCP_HEADER_SIZE + 16) {
        return 0;
    }
    memset(buffer, 0, DHCP_HEADER_SIZE);
    const struct net_config *config = net_config_get();
    buffer[0] = 1;
    buffer[1] = 1;
    buffer[2] = 6;
    buffer[3] = 0;
    write_be32(buffer + 4, transaction);
    write_be16(buffer + 8, 0);
    write_be16(buffer + 10, 0x8000);
    memcpy(buffer + 28, config->mac, 6);
    write_be32(buffer + DHCP_FIXED_SIZE, DHCP_MAGIC_COOKIE);
    u32 position = DHCP_HEADER_SIZE;
    position = append_option(buffer, position, DHCP_OPTION_MESSAGE_TYPE, 1, &message_type);
    u8 client_identifier[7];
    client_identifier[0] = 1;
    memcpy(client_identifier + 1, config->mac, 6);
    position = append_option(buffer, position, 61, 7, client_identifier);
    u8 parameters[4] = {DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
                        DHCP_OPTION_LEASE_TIME};
    position = append_option(buffer, position, DHCP_OPTION_PARAMETER_LIST, 4, parameters);
    if (requested != 0) {
        u8 requested_bytes[4];
        write_be32(requested_bytes, requested);
        position = append_option(buffer, position, DHCP_OPTION_REQUESTED_IP, 4, requested_bytes);
    }
    if (server != 0) {
        u8 server_bytes[4];
        write_be32(server_bytes, server);
        position = append_option(buffer, position, DHCP_OPTION_SERVER_ID, 4, server_bytes);
    }
    buffer[position++] = DHCP_OPTION_END;
    return position;
}

u32 dhcp_build_discover(u8 *buffer, u32 capacity, u32 transaction) {
    return build_message(buffer, capacity, transaction, DHCP_MESSAGE_DISCOVER, 0, 0);
}

u32 dhcp_build_request(u8 *buffer, u32 capacity, u32 transaction, u32 requested, u32 server) {
    return build_message(buffer, capacity, transaction, DHCP_MESSAGE_REQUEST, requested, server);
}

static int send_broadcast(const u8 *payload, u32 length) {
    static u8 packet[NET_MTU];
    const struct net_config *config = net_config_get();
    u32 total = length + IPV4_HEADER_SIZE;
    if (total > NET_MTU) {
        return -1;
    }
    memset(packet, 0, IPV4_HEADER_SIZE);
    packet[0] = 0x45;
    packet[2] = (u8)(total >> 8);
    packet[3] = (u8)(total & 0xFF);
    packet[6] = 0x40;
    packet[8] = 64;
    packet[9] = IPV4_PROTOCOL_UDP;
    write_be32(packet + 12, config->ip);
    write_be32(packet + 16, DHCP_BROADCAST_IP);
    u16 checksum = (u16)ipv4_checksum(packet, IPV4_HEADER_SIZE);
    packet[10] = (u8)(checksum >> 8);
    packet[11] = (u8)(checksum & 0xFF);
    memcpy(packet + IPV4_HEADER_SIZE, payload, length);
    static const u8 broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (eth_send(broadcast_mac, ETH_TYPE_IPV4, packet, total) != 0) {
        return -1;
    }
    messages_sent++;
    return 0;
}

static int send_message(const u8 *message, u32 length) {
    static u8 datagram[DHCP_MAX_MESSAGE];
    write_be16(datagram, DHCP_CLIENT_PORT);
    write_be16(datagram + 2, DHCP_SERVER_PORT);
    write_be16(datagram + 4, (u16)(length + 8));
    write_be16(datagram + 6, 0);
    memcpy(datagram + 8, message, length);
    return send_broadcast(datagram, length + 8);
}

bool dhcp_parse_offer(const u8 *message, u32 length, u32 transaction, struct dhcp_lease *lease,
                      u8 *message_type) {
    if (message == NULL || lease == NULL || length < DHCP_HEADER_SIZE) {
        return false;
    }
    if (message[0] != 2 || read_be32(message + 4) != transaction) {
        return false;
    }
    if (read_be32(message + DHCP_FIXED_SIZE) != DHCP_MAGIC_COOKIE) {
        return false;
    }
    memset(lease, 0, sizeof(struct dhcp_lease));
    lease->address = read_be32(message + 16);
    u32 position = DHCP_HEADER_SIZE;
    u8 type = 0;
    while (position < length) {
        u8 option = message[position++];
        if (option == DHCP_OPTION_PAD) {
            continue;
        }
        if (option == DHCP_OPTION_END) {
            break;
        }
        if (position >= length) {
            return false;
        }
        u8 option_length = message[position++];
        if (position + option_length > length) {
            return false;
        }
        const u8 *value = message + position;
        if (option == DHCP_OPTION_MESSAGE_TYPE && option_length == 1) {
            type = value[0];
        } else if (option == DHCP_OPTION_SUBNET_MASK && option_length == 4) {
            lease->netmask = read_be32(value);
        } else if (option == DHCP_OPTION_ROUTER && option_length >= 4) {
            lease->gateway = read_be32(value);
        } else if (option == DHCP_OPTION_DNS && option_length >= 4) {
            lease->dns = read_be32(value);
        } else if (option == DHCP_OPTION_SERVER_ID && option_length == 4) {
            lease->server = read_be32(value);
        } else if (option == DHCP_OPTION_LEASE_TIME && option_length == 4) {
            lease->lease_seconds = read_be32(value);
        }
        position += option_length;
    }
    if (lease->address == 0 || type == 0) {
        return false;
    }
    lease->valid = true;
    if (message_type != NULL) {
        *message_type = type;
    }
    return true;
}

bool dhcp_apply_lease(const struct dhcp_lease *lease) {
    if (lease == NULL || !lease->valid || lease->address == 0) {
        return false;
    }
    struct net_config *config = (struct net_config *)net_config_get();
    config->ip = lease->address;
    config->netmask = lease->netmask == 0 ? NET_IPV4(255, 255, 255, 0) : lease->netmask;
    config->gateway = lease->gateway == 0 ? NET_IPV4(10, 0, 2, 2) : lease->gateway;
    config->dns = lease->dns == 0 ? NET_IPV4(10, 0, 2, 3) : lease->dns;
    arp_cache_clear();
    current_lease = *lease;
    current_lease.valid = true;
    return true;
}

int dhcp_discover(u32 timeout_ms) {
    if (!net_ready()) {
        failures++;
        return -1;
    }
    memset(&current_lease, 0, sizeof(current_lease));
    u32 transaction = transaction_counter++;
    u8 message[DHCP_MAX_MESSAGE];
    u32 message_length = dhcp_build_discover(message, sizeof(message), transaction);
    if (message_length == 0) {
        failures++;
        return -1;
    }
    int udp_index = udp_bind(DHCP_CLIENT_PORT);
    if (udp_index < 0) {
        failures++;
        return -1;
    }
    if (send_message(message, message_length) != 0) {
        udp_unbind(DHCP_CLIENT_PORT);
        failures++;
        return -1;
    }
    u8 response[DHCP_MAX_MESSAGE];
    u32 source_ip = 0;
    u16 source_port = 0;
    struct dhcp_lease offered;
    bool offered_valid = false;
    for (u32 attempt = 0; attempt < 8; attempt++) {
        int received = udp_recv((u32)udp_index, response, sizeof(response), timeout_ms / 8 + 1,
                                &source_ip, &source_port);
        if (received <= 0) {
            continue;
        }
        messages_received++;
        u8 message_type = 0;
        if (dhcp_parse_offer(response, (u32)received, transaction, &offered, &message_type) &&
            message_type == DHCP_MESSAGE_OFFER) {
            offered_valid = true;
            break;
        }
    }
    if (!offered_valid) {
        udp_unbind(DHCP_CLIENT_PORT);
        failures++;
        return -1;
    }
    u32 request_length = dhcp_build_request(message, sizeof(message), transaction,
                                            offered.address, offered.server);
    if (request_length == 0 || send_message(message, request_length) != 0) {
        udp_unbind(DHCP_CLIENT_PORT);
        failures++;
        return -1;
    }
    bool acknowledged = false;
    for (u32 attempt = 0; attempt < 8; attempt++) {
        int received = udp_recv((u32)udp_index, response, sizeof(response), timeout_ms / 8 + 1,
                                &source_ip, &source_port);
        if (received <= 0) {
            continue;
        }
        messages_received++;
        u8 message_type = 0;
        struct dhcp_lease confirmed;
        if (dhcp_parse_offer(response, (u32)received, transaction, &confirmed, &message_type) &&
            message_type == DHCP_MESSAGE_ACK) {
            if (confirmed.netmask == 0) {
                confirmed.netmask = offered.netmask;
            }
            if (confirmed.gateway == 0) {
                confirmed.gateway = offered.gateway;
            }
            if (confirmed.dns == 0) {
                confirmed.dns = offered.dns;
            }
            if (confirmed.server == 0) {
                confirmed.server = offered.server;
            }
            if (dhcp_apply_lease(&confirmed)) {
                acknowledged = true;
                break;
            }
        }
    }
    udp_unbind(DHCP_CLIENT_PORT);
    if (!acknowledged) {
        failures++;
        return -1;
    }
    return 0;
}

const struct dhcp_lease *dhcp_current_lease(void) {
    return &current_lease;
}

u32 dhcp_messages_sent(void) {
    return messages_sent;
}

u32 dhcp_messages_received(void) {
    return messages_received;
}

u32 dhcp_failures(void) {
    return failures;
}
