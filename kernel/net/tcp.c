#include "tcp.h"
#include "arp.h"
#include "ipv4.h"
#include "lib.h"
#include "net.h"
#include "string.h"
#include "thread.h"

#define TCP_PSEUDO_HEADER_SIZE 12
#define TCP_MAX_SEGMENT (TCP_PSEUDO_HEADER_SIZE + TCP_HEADER_SIZE + TCP_MSS)

struct tcp_connection {
    bool used;
    bool accepted;
    enum tcp_state state;
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    u32 sequence_send_unacknowledged;
    u32 sequence_send_next;
    u32 send_window;
    u32 sequence_receive_next;
    u32 receive_window;
    u32 initial_sequence;
    u32 retransmit_timer;
    u32 retransmit_timeout;
    u32 retransmit_attempts;
    u32 duplicate_acknowledgements;
    u32 time_wait_timer;
    u8 send_buffer[TCP_SEND_BUFFER];
    u32 send_length;
    u8 receive_buffer[TCP_RECV_BUFFER];
    u32 receive_length;
    bool fin_sent;
    bool fin_received;
};

static struct tcp_connection connections[TCP_MAX_CONNECTIONS];
static u32 sequence_counter = 0x5000;
static u32 retransmit_count = 0;
static u32 segments_sent = 0;
static u32 segments_received = 0;
static u32 checksum_errors = 0;
static bool transmit_enabled = true;

static u32 next_initial_sequence(void) {
    sequence_counter += 0x1000;
    return sequence_counter + 0x5A5A0000U;
}

static u32 read_be32(const u8 *data) {
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static void write_be32(u8 *data, u32 value) {
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)(value & 0xFF);
}

static void write_be16(u8 *data, u16 value) {
    data[0] = (u8)(value >> 8);
    data[1] = (u8)(value & 0xFF);
}

static u16 read_be16(const u8 *data) {
    return (u16)(((u16)data[0] << 8) | data[1]);
}

bool tcp_seq_less(u32 first, u32 second) {
    return (i32)(first - second) < 0;
}

u32 tcp_window_clamp(u32 window, u32 unacked, u32 available, u32 mss) {
    if (window <= unacked) {
        return 0;
    }
    u32 allowed = window - unacked;
    if (allowed > available) {
        allowed = available;
    }
    if (allowed > mss) {
        allowed = mss;
    }
    return allowed;
}

const char *tcp_state_name(u32 state) {
    switch (state) {
        case TCP_STATE_CLOSED:
            return "closed";
        case TCP_STATE_LISTEN:
            return "listen";
        case TCP_STATE_SYN_SENT:
            return "syn_sent";
        case TCP_STATE_SYN_RECV:
            return "syn_recv";
        case TCP_STATE_ESTABLISHED:
            return "established";
        case TCP_STATE_FIN_WAIT_1:
            return "fin_wait_1";
        case TCP_STATE_FIN_WAIT_2:
            return "fin_wait_2";
        case TCP_STATE_CLOSE_WAIT:
            return "close_wait";
        case TCP_STATE_LAST_ACK:
            return "last_ack";
        case TCP_STATE_TIME_WAIT:
            return "time_wait";
        case TCP_STATE_CLOSING:
            return "closing";
        default:
            return "unknown";
    }
}

void tcp_init(void) {
    memset(connections, 0, sizeof(connections));
    sequence_counter = 0x5000;
    retransmit_count = 0;
    segments_sent = 0;
    segments_received = 0;
    checksum_errors = 0;
}

u32 tcp_checksum(u32 source_ip, u32 destination_ip, const u8 *segment, u32 length) {
    static u8 buffer[TCP_PSEUDO_HEADER_SIZE + 1600];
    if (segment == NULL || length > 1600) {
        return 0xFFFF;
    }
    write_be32(buffer, source_ip);
    write_be32(buffer + 4, destination_ip);
    buffer[8] = 0;
    buffer[9] = 6;
    write_be16(buffer + 10, (u16)length);
    memcpy(buffer + TCP_PSEUDO_HEADER_SIZE, segment, length);
    return net_checksum(buffer, TCP_PSEUDO_HEADER_SIZE + length);
}

bool tcp_parse_header(const u8 *segment, u32 length, struct tcp_header_view *view) {
    if (segment == NULL || view == NULL || length < TCP_HEADER_SIZE) {
        return false;
    }
    view->source_port = read_be16(segment);
    view->destination_port = read_be16(segment + 2);
    view->sequence = read_be32(segment + 4);
    view->acknowledgement = read_be32(segment + 8);
    view->data_offset = (u8)((segment[12] >> 4) * 4);
    view->flags = segment[13];
    view->window = read_be16(segment + 14);
    view->checksum = read_be16(segment + 16);
    view->urgent = read_be16(segment + 18);
    if (view->data_offset < TCP_HEADER_SIZE || view->data_offset > length) {
        return false;
    }
    return true;
}

static struct tcp_connection *find_connection(int id) {
    if (id < 1 || id > TCP_MAX_CONNECTIONS) {
        return NULL;
    }
    struct tcp_connection *connection = &connections[id - 1];
    return connection->used ? connection : NULL;
}

static struct tcp_connection *allocate_connection(void) {
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        if (!connections[index].used) {
            memset(&connections[index], 0, sizeof(struct tcp_connection));
            connections[index].used = true;
            connections[index].state = TCP_STATE_CLOSED;
            connections[index].send_window = TCP_DEFAULT_WINDOW;
            connections[index].receive_window = TCP_DEFAULT_WINDOW;
            connections[index].retransmit_timeout = TCP_INITIAL_RTO;
            return &connections[index];
        }
    }
    return NULL;
}

static void release_connection(struct tcp_connection *connection) {
    if (connection != NULL) {
        memset(connection, 0, sizeof(struct tcp_connection));
    }
}

static int connection_id(struct tcp_connection *connection) {
    return (int)(connection - connections) + 1;
}

static struct tcp_connection *find_listener(u16 port) {
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        if (connections[index].used && connections[index].state == TCP_STATE_LISTEN &&
            connections[index].local_port == port) {
            return &connections[index];
        }
    }
    return NULL;
}

static struct tcp_connection *find_established_for_port(u32 local_ip, u16 local_port,
                                                        u32 remote_ip, u16 remote_port) {
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        struct tcp_connection *connection = &connections[index];
        if (!connection->used) {
            continue;
        }
        if (connection->state == TCP_STATE_LISTEN) {
            continue;
        }
        if (connection->local_ip == local_ip && connection->local_port == local_port &&
            connection->remote_ip == remote_ip && connection->remote_port == remote_port) {
            return connection;
        }
    }
    return NULL;
}

static int transmit_segment(struct tcp_connection *connection, u8 flags, u32 sequence,
                            u32 acknowledgement, const void *payload, u32 length) {
    static u8 segment[TCP_HEADER_SIZE + TCP_MSS];
    if (!transmit_enabled) {
        segments_sent++;
        connection->retransmit_timer = 0;
        return 0;
    }
    if (length > TCP_MSS) {
        length = TCP_MSS;
    }
    memset(segment, 0, TCP_HEADER_SIZE);
    write_be16(segment, connection->local_port);
    write_be16(segment + 2, connection->remote_port);
    write_be32(segment + 4, sequence);
    write_be32(segment + 8, acknowledgement);
    segment[12] = (u8)((TCP_HEADER_SIZE / 4) << 4);
    segment[13] = flags;
    write_be16(segment + 14, (u16)(TCP_RECV_BUFFER - connection->receive_length));
    if (payload != NULL && length > 0) {
        memcpy(segment + TCP_HEADER_SIZE, payload, length);
    }
    u32 total = TCP_HEADER_SIZE + length;
    u16 checksum = (u16)tcp_checksum(connection->local_ip, connection->remote_ip, segment, total);
    write_be16(segment + 16, checksum);
    if (ipv4_send(connection->remote_ip, IPV4_PROTOCOL_TCP, segment, total) != 0) {
        return -1;
    }
    segments_sent++;
    connection->retransmit_timer = 0;
    return 0;
}

static void send_acknowledgement(struct tcp_connection *connection) {
    transmit_segment(connection, TCP_FLAG_ACK, connection->sequence_send_next,
                     connection->sequence_receive_next, NULL, 0);
}

static int flush_send_buffer(struct tcp_connection *connection) {
    u32 unacknowledged = connection->sequence_send_next - connection->sequence_send_unacknowledged;
    while (connection->send_length > 0) {
        u32 allowed = tcp_window_clamp(connection->send_window, unacknowledged,
                                       connection->send_length, TCP_MSS);
        if (allowed == 0) {
            break;
        }
        u32 sequence = connection->sequence_send_next;
        u8 flags = TCP_FLAG_ACK;
        if (allowed == connection->send_length) {
            flags |= TCP_FLAG_PSH;
        }
        if (transmit_segment(connection, flags, sequence, connection->sequence_receive_next,
                             connection->send_buffer, allowed) != 0) {
            return -1;
        }
        memmove(connection->send_buffer, connection->send_buffer + allowed,
                connection->send_length - allowed);
        connection->send_length -= allowed;
        connection->sequence_send_next += allowed;
        unacknowledged += allowed;
    }
    return 0;
}

static int retransmit_oldest(struct tcp_connection *connection) {
    u32 unacknowledged = connection->sequence_send_next - connection->sequence_send_unacknowledged;
    u32 length = unacknowledged;
    if (length > TCP_MSS) {
        length = TCP_MSS;
    }
    if (length == 0) {
        length = 1;
    }
    retransmit_count++;
    connection->retransmit_attempts++;
    u8 flags = TCP_FLAG_ACK;
    if (connection->state == TCP_STATE_SYN_SENT) {
        flags = TCP_FLAG_SYN;
        length = 0;
    } else if (connection->state == TCP_STATE_SYN_RECV) {
        flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
        length = 0;
    }
    const void *payload = length > 0 ? connection->send_buffer : NULL;
    if (length > connection->send_length) {
        length = connection->send_length;
        payload = length > 0 ? connection->send_buffer : NULL;
    }
    transmit_segment(connection, flags, connection->sequence_send_unacknowledged,
                     connection->sequence_receive_next, payload, length);
    return 0;
}

void tcp_tick(void) {
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        struct tcp_connection *connection = &connections[index];
        if (!connection->used) {
            continue;
        }
        if (connection->state == TCP_STATE_TIME_WAIT) {
            connection->time_wait_timer++;
            if (connection->time_wait_timer > 200) {
                release_connection(connection);
            }
            continue;
        }
        if (connection->state == TCP_STATE_SYN_SENT ||
            connection->state == TCP_STATE_SYN_RECV ||
            connection->state == TCP_STATE_ESTABLISHED ||
            connection->state == TCP_STATE_FIN_WAIT_1 ||
            connection->state == TCP_STATE_LAST_ACK ||
            connection->state == TCP_STATE_CLOSING) {
            connection->retransmit_timer++;
            bool pending = connection->state == TCP_STATE_SYN_SENT ||
                           connection->state == TCP_STATE_SYN_RECV ||
                           connection->sequence_send_next != connection->sequence_send_unacknowledged;
            if (pending && connection->retransmit_timer >= connection->retransmit_timeout) {
                connection->retransmit_timer = 0;
                if (connection->retransmit_attempts >= TCP_MAX_RETRIES) {
                    release_connection(connection);
                    continue;
                }
                connection->retransmit_timeout += TCP_INITIAL_RTO;
                retransmit_oldest(connection);
            }
        }
    }
}

int tcp_listen(u16 port) {
    if (port == 0 || !net_ready()) {
        return -1;
    }
    if (find_listener(port) != NULL) {
        return -1;
    }
    struct tcp_connection *connection = allocate_connection();
    if (connection == NULL) {
        return -1;
    }
    const struct net_config *config = net_config_get();
    connection->state = TCP_STATE_LISTEN;
    connection->local_ip = config->ip;
    connection->local_port = port;
    return connection_id(connection);
}

int tcp_test_open_syn_sent(u32 destination, u16 port) {
    struct tcp_connection *connection = allocate_connection();
    if (connection == NULL) {
        return -1;
    }
    const struct net_config *config = net_config_get();
    connection->state = TCP_STATE_SYN_SENT;
    connection->local_ip = config->ip;
    connection->local_port = (u16)(0x4000 + (connection_id(connection) * 7));
    connection->remote_ip = destination;
    connection->remote_port = port;
    connection->initial_sequence = next_initial_sequence();
    connection->sequence_send_unacknowledged = connection->initial_sequence;
    connection->sequence_send_next = connection->initial_sequence + 1;
    connection->sequence_receive_next = 0;
    return connection_id(connection);
}

int tcp_connect(u32 destination, u16 port, u32 timeout_ms) {
    if (!net_ready() || destination == 0 || port == 0) {
        return -1;
    }
    int id = tcp_test_open_syn_sent(destination, port);
    if (id < 0) {
        return -1;
    }
    struct tcp_connection *connection = find_connection(id);
    transmit_segment(connection, TCP_FLAG_SYN, connection->initial_sequence, 0, NULL, 0);
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        net_poll();
        tcp_tick();
        if (connection->state == TCP_STATE_CLOSED && !connection->used) {
            return -1;
        }
        if (connection->state == TCP_STATE_ESTABLISHED) {
            return id;
        }
        net_delay_ms(10);
    }
    release_connection(connection);
    return -1;
}

int tcp_accept(int listen_id, u32 timeout_ms) {
    struct tcp_connection *listener = find_connection(listen_id);
    if (listener == NULL || listener->state != TCP_STATE_LISTEN) {
        return -1;
    }
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
            struct tcp_connection *connection = &connections[index];
            if (connection->used && !connection->accepted &&
                connection->state == TCP_STATE_ESTABLISHED &&
                connection->local_port == listener->local_port) {
                connection->accepted = true;
                return connection_id(connection);
            }
        }
        net_poll();
        tcp_tick();
        net_delay_ms(10);
    }
    return -1;
}

int tcp_send(int connection_id, const void *data, u32 length) {
    struct tcp_connection *connection = find_connection(connection_id);
    if (connection == NULL || data == NULL || length == 0) {
        return -1;
    }
    if (connection->state != TCP_STATE_ESTABLISHED &&
        connection->state != TCP_STATE_CLOSE_WAIT) {
        return -1;
    }
    if (connection->send_length + length > TCP_SEND_BUFFER) {
        return -1;
    }
    memcpy(connection->send_buffer + connection->send_length, data, length);
    connection->send_length += length;
    if (flush_send_buffer(connection) != 0) {
        return -1;
    }
    return (int)length;
}

int tcp_recv(int connection_id, void *buffer, u32 capacity, u32 timeout_ms) {
    struct tcp_connection *connection = find_connection(connection_id);
    if (connection == NULL || buffer == NULL || capacity == 0) {
        return -1;
    }
    u32 iterations = timeout_ms / 10 + 1;
    for (u32 elapsed = 0; elapsed < iterations; elapsed++) {
        if (connection->receive_length > 0) {
            u32 length = connection->receive_length;
            if (length > capacity) {
                length = capacity;
            }
            memcpy(buffer, connection->receive_buffer, length);
            memmove(connection->receive_buffer, connection->receive_buffer + length,
                    connection->receive_length - length);
            connection->receive_length -= length;
            send_acknowledgement(connection);
            return (int)length;
        }
        if (connection->fin_received && connection->receive_length == 0) {
            return 0;
        }
        net_poll();
        tcp_tick();
        net_delay_ms(10);
    }
    return -1;
}

int tcp_close(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    if (connection == NULL) {
        return -1;
    }
    if (connection->state == TCP_STATE_LISTEN || connection->state == TCP_STATE_SYN_SENT) {
        release_connection(connection);
        return 0;
    }
    if (connection->state == TCP_STATE_ESTABLISHED) {
        transmit_segment(connection, TCP_FLAG_FIN | TCP_FLAG_ACK,
                         connection->sequence_send_next, connection->sequence_receive_next,
                         NULL, 0);
        connection->sequence_send_next++;
        connection->fin_sent = true;
        connection->state = TCP_STATE_FIN_WAIT_1;
        for (u32 elapsed = 0; elapsed < 200; elapsed++) {
            net_poll();
            tcp_tick();
            if (connection->state == TCP_STATE_FIN_WAIT_2 ||
                connection->state == TCP_STATE_CLOSED ||
                connection->state == TCP_STATE_TIME_WAIT) {
                break;
            }
            net_delay_ms(10);
        }
        release_connection(connection);
        return 0;
    }
    if (connection->state == TCP_STATE_CLOSE_WAIT) {
        transmit_segment(connection, TCP_FLAG_FIN | TCP_FLAG_ACK,
                         connection->sequence_send_next, connection->sequence_receive_next,
                         NULL, 0);
        connection->sequence_send_next++;
        connection->state = TCP_STATE_LAST_ACK;
        for (u32 elapsed = 0; elapsed < 100; elapsed++) {
            net_poll();
            tcp_tick();
            if (!connection->used) {
                break;
            }
            net_delay_ms(10);
        }
        release_connection(connection);
        return 0;
    }
    release_connection(connection);
    return 0;
}

static struct tcp_connection *match_incoming(const struct tcp_header_view *view, u32 source_ip) {
    const struct net_config *config = net_config_get();
    struct tcp_connection *connection = find_established_for_port(config->ip, view->destination_port,
                                                                  source_ip, view->source_port);
    if (connection != NULL) {
        return connection;
    }
    return NULL;
}

static void handle_listen(struct tcp_connection *listener, const struct tcp_header_view *view,
                          const u8 *payload, u32 payload_length, u32 source_ip) {
    (void)payload;
    (void)payload_length;
    if ((view->flags & TCP_FLAG_SYN) == 0) {
        return;
    }
    struct tcp_connection *connection = find_established_for_port(listener->local_ip,
                                                                  listener->local_port,
                                                                  source_ip, view->source_port);
    if (connection == NULL) {
        connection = allocate_connection();
        if (connection == NULL) {
            return;
        }
        connection->local_ip = listener->local_ip;
        connection->local_port = listener->local_port;
        connection->remote_ip = source_ip;
        connection->remote_port = view->source_port;
        connection->send_window = view->window == 0 ? TCP_DEFAULT_WINDOW : view->window;
    }
    connection->state = TCP_STATE_SYN_RECV;
    connection->sequence_receive_next = view->sequence + 1;
    connection->initial_sequence = next_initial_sequence();
    connection->sequence_send_unacknowledged = connection->initial_sequence;
    connection->sequence_send_next = connection->initial_sequence + 1;
    transmit_segment(connection, TCP_FLAG_SYN | TCP_FLAG_ACK, connection->initial_sequence,
                     connection->sequence_receive_next, NULL, 0);
}

static void handle_syn_sent(struct tcp_connection *connection, const struct tcp_header_view *view) {
    if ((view->flags & TCP_FLAG_RST) != 0) {
        release_connection(connection);
        return;
    }
    if ((view->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        if (view->acknowledgement != connection->sequence_send_next) {
            return;
        }
        connection->sequence_send_unacknowledged = view->acknowledgement;
        connection->sequence_receive_next = view->sequence + 1;
        connection->send_window = view->window == 0 ? TCP_DEFAULT_WINDOW : view->window;
        connection->state = TCP_STATE_ESTABLISHED;
        connection->retransmit_attempts = 0;
        send_acknowledgement(connection);
    }
}

static void handle_syn_recv(struct tcp_connection *connection, const struct tcp_header_view *view) {
    if ((view->flags & TCP_FLAG_RST) != 0) {
        release_connection(connection);
        return;
    }
    if ((view->flags & TCP_FLAG_ACK) != 0 &&
        view->acknowledgement == connection->sequence_send_next) {
        connection->state = TCP_STATE_ESTABLISHED;
        connection->sequence_send_unacknowledged = view->acknowledgement;
        connection->retransmit_attempts = 0;
        connection->send_window = view->window == 0 ? TCP_DEFAULT_WINDOW : view->window;
    }
}

static void handle_established(struct tcp_connection *connection, const struct tcp_header_view *view,
                               const u8 *payload, u32 payload_length) {
    if ((view->flags & TCP_FLAG_RST) != 0) {
        release_connection(connection);
        return;
    }
    if ((view->flags & TCP_FLAG_ACK) != 0) {
        u32 acknowledgement = view->acknowledgement;
        u32 unacknowledged = connection->sequence_send_next - connection->sequence_send_unacknowledged;
        if (tcp_seq_less(connection->sequence_send_unacknowledged, acknowledgement) &&
            !tcp_seq_less(connection->sequence_send_next, acknowledgement)) {
            u32 acknowledged = acknowledgement - connection->sequence_send_unacknowledged;
            if (acknowledged > unacknowledged) {
                acknowledged = unacknowledged;
            }
            connection->sequence_send_unacknowledged = acknowledgement;
            connection->duplicate_acknowledgements = 0;
            connection->retransmit_attempts = 0;
            connection->retransmit_timeout = TCP_INITIAL_RTO;
            if (connection->send_length >= acknowledged) {
                connection->send_length -= acknowledged;
            } else {
                connection->send_length = 0;
            }
            flush_send_buffer(connection);
        } else if (acknowledgement == connection->sequence_send_unacknowledged &&
                   connection->sequence_send_next != connection->sequence_send_unacknowledged) {
            connection->duplicate_acknowledgements++;
            if (connection->duplicate_acknowledgements == 3) {
                retransmit_count++;
                retransmit_oldest(connection);
                connection->duplicate_acknowledgements = 0;
            }
        }
        connection->send_window = view->window == 0 ? TCP_DEFAULT_WINDOW : view->window;
    }
    if (payload_length > 0 && view->sequence == connection->sequence_receive_next) {
        u32 copy = payload_length;
        if (connection->receive_length + copy > TCP_RECV_BUFFER) {
            copy = TCP_RECV_BUFFER - connection->receive_length;
        }
        if (copy > 0) {
            memcpy(connection->receive_buffer + connection->receive_length, payload, copy);
            connection->receive_length += copy;
        }
        connection->sequence_receive_next += payload_length;
        send_acknowledgement(connection);
    }
    if ((view->flags & TCP_FLAG_FIN) != 0) {
        connection->sequence_receive_next = view->sequence + payload_length + 1;
        connection->fin_received = true;
        send_acknowledgement(connection);
        if (connection->state == TCP_STATE_ESTABLISHED) {
            connection->state = TCP_STATE_CLOSE_WAIT;
        } else if (connection->state == TCP_STATE_FIN_WAIT_1) {
            connection->state = TCP_STATE_CLOSING;
        } else if (connection->state == TCP_STATE_FIN_WAIT_2) {
            connection->state = TCP_STATE_TIME_WAIT;
            connection->time_wait_timer = 0;
        }
    }
}

void tcp_receive(const u8 *segment, u32 length, u32 source_ip) {
    if (segment == NULL || length < TCP_HEADER_SIZE) {
        return;
    }
    struct tcp_header_view view;
    if (!tcp_parse_header(segment, length, &view)) {
        return;
    }
    u32 total = length;
    if (tcp_checksum(source_ip, net_config_get()->ip, segment, total) != 0) {
        checksum_errors++;
        return;
    }
    segments_received++;
    u32 payload_length = length - view.data_offset;
    const u8 *payload = segment + view.data_offset;
    struct tcp_connection *connection = match_incoming(&view, source_ip);
    if (connection != NULL) {
        if (connection->state == TCP_STATE_SYN_RECV) {
            handle_syn_recv(connection, &view);
            if (connection->state == TCP_STATE_ESTABLISHED) {
                handle_established(connection, &view, payload, payload_length);
            }
            return;
        }
        if (connection->state == TCP_STATE_SYN_SENT) {
            handle_syn_sent(connection, &view);
            return;
        }
        handle_established(connection, &view, payload, payload_length);
        return;
    }
    struct tcp_connection *listener = find_listener(view.destination_port);
    if (listener != NULL) {
        handle_listen(listener, &view, payload, payload_length, source_ip);
        return;
    }
    if ((view.flags & TCP_FLAG_RST) == 0) {
        struct tcp_connection *reset = allocate_connection();
        if (reset != NULL) {
            const struct net_config *config = net_config_get();
            reset->local_ip = config->ip;
            reset->local_port = view.destination_port;
            reset->remote_ip = source_ip;
            reset->remote_port = view.source_port;
            reset->sequence_send_next = view.acknowledgement;
            reset->sequence_receive_next = view.sequence + payload_length;
            transmit_segment(reset, TCP_FLAG_RST | TCP_FLAG_ACK, reset->sequence_send_next,
                             reset->sequence_receive_next, NULL, 0);
            release_connection(reset);
        }
    }
}

u32 tcp_state_of(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? TCP_STATE_CLOSED : (u32)connection->state;
}

u32 tcp_connection_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        if (connections[index].used) {
            count++;
        }
    }
    return count;
}

u32 tcp_local_port(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->local_port;
}

u32 tcp_remote_address(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->remote_ip;
}

u32 tcp_remote_port(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->remote_port;
}

u32 tcp_retransmit_count(void) {
    return retransmit_count;
}

u32 tcp_segments_sent(void) {
    return segments_sent;
}

u32 tcp_segments_received(void) {
    return segments_received;
}

u32 tcp_checksum_errors(void) {
    return checksum_errors;
}

u32 tcp_test_send_sequence(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->sequence_send_next;
}

u32 tcp_test_receive_sequence(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->sequence_receive_next;
}

void tcp_test_set_transmit(bool enabled) {
    transmit_enabled = enabled;
}

bool tcp_can_receive(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    if (connection == NULL) {
        return false;
    }
    if (connection->state == TCP_STATE_LISTEN) {
        return tcp_accept_pending(connection_id);
    }
    if (connection->receive_length > 0 || connection->fin_received) {
        return true;
    }
    return connection->state == TCP_STATE_CLOSE_WAIT;
}

bool tcp_can_send(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    if (connection == NULL) {
        return false;
    }
    if (connection->state != TCP_STATE_ESTABLISHED && connection->state != TCP_STATE_CLOSE_WAIT) {
        return false;
    }
    return connection->send_length < TCP_SEND_BUFFER;
}

bool tcp_accept_pending(int listen_id) {
    struct tcp_connection *listener = find_connection(listen_id);
    if (listener == NULL || listener->state != TCP_STATE_LISTEN) {
        return false;
    }
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        struct tcp_connection *connection = &connections[index];
        if (connection->used && !connection->accepted &&
            connection->state == TCP_STATE_ESTABLISHED &&
            connection->local_port == listener->local_port) {
            return true;
        }
    }
    return false;
}

u32 tcp_receive_pending(int connection_id) {
    struct tcp_connection *connection = find_connection(connection_id);
    return connection == NULL ? 0 : connection->receive_length;
}
