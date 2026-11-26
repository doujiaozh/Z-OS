#ifndef Z_OS_TCP_H
#define Z_OS_TCP_H

#include "types.h"

#define TCP_MAX_CONNECTIONS 8
#define TCP_HEADER_SIZE 20
#define TCP_SEND_BUFFER 2048
#define TCP_RECV_BUFFER 2048
#define TCP_MSS 536
#define TCP_DEFAULT_WINDOW 2048
#define TCP_MAX_RETRIES 6
#define TCP_INITIAL_RTO 30

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

enum tcp_state {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN = 1,
    TCP_STATE_SYN_SENT = 2,
    TCP_STATE_SYN_RECV = 3,
    TCP_STATE_ESTABLISHED = 4,
    TCP_STATE_FIN_WAIT_1 = 5,
    TCP_STATE_FIN_WAIT_2 = 6,
    TCP_STATE_CLOSE_WAIT = 7,
    TCP_STATE_LAST_ACK = 8,
    TCP_STATE_TIME_WAIT = 9,
    TCP_STATE_CLOSING = 10
};

struct tcp_header_view {
    u16 source_port;
    u16 destination_port;
    u32 sequence;
    u32 acknowledgement;
    u8 data_offset;
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urgent;
};

void tcp_init(void);
int tcp_listen(u16 port);
int tcp_connect(u32 destination, u16 port, u32 timeout_ms);
int tcp_accept(int listen_id, u32 timeout_ms);
int tcp_send(int connection_id, const void *data, u32 length);
int tcp_recv(int connection_id, void *buffer, u32 capacity, u32 timeout_ms);
int tcp_close(int connection_id);
void tcp_receive(const u8 *segment, u32 length, u32 source_ip);
void tcp_tick(void);
u32 tcp_checksum(u32 source_ip, u32 destination_ip, const u8 *segment, u32 length);
bool tcp_seq_less(u32 first, u32 second);
bool tcp_parse_header(const u8 *segment, u32 length, struct tcp_header_view *view);
u32 tcp_window_clamp(u32 window, u32 unacked, u32 available, u32 mss);
u32 tcp_state_of(int connection_id);
u32 tcp_connection_count(void);
u32 tcp_local_port(int connection_id);
u32 tcp_remote_address(int connection_id);
u32 tcp_remote_port(int connection_id);
u32 tcp_retransmit_count(void);
u32 tcp_segments_sent(void);
u32 tcp_segments_received(void);
u32 tcp_checksum_errors(void);
int tcp_test_open_syn_sent(u32 destination, u16 port);
u32 tcp_test_send_sequence(int connection_id);
u32 tcp_test_receive_sequence(int connection_id);
void tcp_test_set_transmit(bool enabled);
const char *tcp_state_name(u32 state);
bool tcp_can_receive(int connection_id);
bool tcp_can_send(int connection_id);
bool tcp_accept_pending(int listen_id);
u32 tcp_receive_pending(int connection_id);

#endif
