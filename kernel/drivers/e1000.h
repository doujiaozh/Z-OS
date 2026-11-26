#ifndef Z_OS_E1000_H
#define Z_OS_E1000_H

#include "types.h"

#define E1000_RX_DESCRIPTORS 32
#define E1000_TX_DESCRIPTORS 16
#define E1000_BUFFER_SIZE 2048
#define E1000_VENDOR_INTEL 0x8086
#define E1000_DEVICE_82540EM 0x100E

struct e1000_device {
    bool present;
    bool initialized;
    u16 vendor;
    u16 device;
    u8 bus;
    u8 slot;
    u8 function;
    u8 irq;
    u64 mmio_physical;
    volatile u32 *registers;
    u8 mac[6];
    u64 rx_descriptor_physical;
    u64 tx_descriptor_physical;
    u64 rx_buffer_physical[E1000_RX_DESCRIPTORS];
    u64 tx_buffer_physical[E1000_TX_DESCRIPTORS];
    struct e1000_rx_descriptor *rx_descriptors;
    struct e1000_tx_descriptor *tx_descriptors;
    u32 rx_descriptor_count;
    u32 tx_descriptor_count;
    u32 rx_tail;
    u32 tx_tail;
    u32 packets_received;
    u32 packets_sent;
    struct e1000_frame *frames;
    u32 frame_head;
    u32 frame_tail;
    u32 frame_count;
};

struct e1000_frame {
    u16 length;
    u8 data[1600];
};

struct e1000_rx_descriptor {
    u64 address;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
};

struct e1000_tx_descriptor {
    u64 address;
    u16 length;
    u8 checksum_offset;
    u8 command;
    u8 status;
    u8 checksum_start;
    u16 special;
};

int e1000_init(void);
bool e1000_present(void);
const struct e1000_device *e1000_device_state(void);
u8 e1000_irq_line(void);
void e1000_irq_handler(void);
void e1000_get_mac(u8 *mac);
int e1000_transmit(const void *frame, u32 length);
u32 e1000_receive(void *buffer, u32 capacity);
u32 e1000_dequeue_frame(void *buffer, u32 capacity);
u32 e1000_pending_frames(void);
void e1000_poll(void);
const char *e1000_link_state(void);

#endif
