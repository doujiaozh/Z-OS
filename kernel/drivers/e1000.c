#include "e1000.h"
#include "console.h"
#include "heap.h"
#include "io.h"
#include "lib.h"
#include "pci.h"
#include "pmm.h"
#include "pic.h"
#include "string.h"
#include "vmm.h"

#define E1000_REG_CTRL 0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_ICR 0x00C0
#define E1000_REG_IMS 0x00D0
#define E1000_REG_IMC 0x00D8
#define E1000_REG_RCTL 0x0100
#define E1000_REG_TCTL 0x0400
#define E1000_REG_TIPG 0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH 0x2810
#define E1000_REG_RDT 0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH 0x3810
#define E1000_REG_TDT 0x3818
#define E1000_REG_MTA 0x5200
#define E1000_REG_RAL 0x5400
#define E1000_REG_RAH 0x5404

#define E1000_CTRL_FD 0x00000001
#define E1000_CTRL_LRST 0x00000008
#define E1000_CTRL_ASDE 0x00000020
#define E1000_CTRL_SLU 0x00000040
#define E1000_CTRL_RST 0x04000000

#define E1000_STATUS_LU 0x00000002

#define E1000_RCTL_EN 0x00000002
#define E1000_RCTL_BAM 0x00008000
#define E1000_RCTL_BSIZE_2048 0x00000000
#define E1000_RCTL_SECRC 0x04000000

#define E1000_TCTL_EN 0x00000002
#define E1000_TCTL_PSP 0x00000008
#define E1000_TCTL_CT_15 0x00000100
#define E1000_TCTL_COLD_FD 0x00040000
#define E1000_FRAME_QUEUE_DEPTH 8

#define E1000_TXD_CMD_EOP 0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS 0x08
#define E1000_TXD_STATUS_DD 0x01
#define E1000_RXD_STATUS_DD 0x01
#define E1000_RXD_STATUS_EOP 0x02

#define E1000_ICR_TXDW 0x00000001
#define E1000_ICR_LSC 0x00000004
#define E1000_ICR_RXSEQ 0x00000008
#define E1000_ICR_RXDMT0 0x00000010
#define E1000_ICR_RXT0 0x00000080

#define E1000_DEVICE_MAP_OFFSET 0x00800000ULL
#define E1000_MMIO_SIZE 0x00020000ULL

static struct e1000_device device;

static u32 e1000_read(u32 offset) {
    return device.registers[offset / 4];
}

static void e1000_write(u32 offset, u32 value) {
    device.registers[offset / 4] = value;
}

static void e1000_read_mac(void) {
    u32 low = e1000_read(E1000_REG_RAL);
    u32 high = e1000_read(E1000_REG_RAH);
    device.mac[0] = (u8)(low & 0xFF);
    device.mac[1] = (u8)((low >> 8) & 0xFF);
    device.mac[2] = (u8)((low >> 16) & 0xFF);
    device.mac[3] = (u8)((low >> 24) & 0xFF);
    device.mac[4] = (u8)(high & 0xFF);
    device.mac[5] = (u8)((high >> 8) & 0xFF);
    bool valid = false;
    for (u32 index = 0; index < 6; index++) {
        if (device.mac[index] != 0) {
            valid = true;
            break;
        }
    }
    if (!valid) {
        device.mac[0] = 0x52;
        device.mac[1] = 0x54;
        device.mac[2] = 0x00;
        device.mac[3] = 0x12;
        device.mac[4] = 0x34;
        device.mac[5] = 0x56;
    }
}

static int e1000_map_registers(u64 physical) {
    u64 base = vmm_device_base() + E1000_DEVICE_MAP_OFFSET;
    u64 pages = E1000_MMIO_SIZE / PAGE_SIZE;
    for (u64 index = 0; index < pages; index++) {
        u64 virtual_address = base + index * PAGE_SIZE;
        u64 frame = physical + index * PAGE_SIZE;
        if (vmm_map_device_page(virtual_address, frame,
                                PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_WRITE_THROUGH) != 0) {
            return -1;
        }
    }
    device.registers = (volatile u32 *)(usize)base;
    device.mmio_physical = physical;
    return 0;
}

int e1000_init(void) {
    memset(&device, 0, sizeof(device));
    struct pci_device found;
    if (!pci_find_device(PCI_VENDOR_INTEL, E1000_DEVICE_82540EM, &found)) {
        return -1;
    }
    device.vendor = found.vendor;
    device.device = found.device;
    device.bus = found.bus;
    device.slot = found.slot;
    device.function = found.function;
    device.irq = found.interrupt_line;
    pci_enable_device(&found);
    u64 bar = pci_bar_physical(&found, 0);
    if (bar == 0) {
        return -1;
    }
    if (e1000_map_registers(bar) != 0) {
        return -1;
    }
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (u32 spin = 0; spin < 100000; spin++) {
        if ((e1000_read(E1000_REG_CTRL) & E1000_CTRL_RST) == 0) {
            break;
        }
    }
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    e1000_write(E1000_REG_CTRL, E1000_CTRL_SLU | E1000_CTRL_ASDE | E1000_CTRL_FD);
    for (u32 index = 0; index < 128; index++) {
        e1000_write(E1000_REG_MTA + index * 4, 0);
    }
    e1000_read_mac();
    u32 ral = (u32)device.mac[0] | ((u32)device.mac[1] << 8) |
              ((u32)device.mac[2] << 16) | ((u32)device.mac[3] << 24);
    u32 rah = (u32)device.mac[4] | ((u32)device.mac[5] << 8) | 0x80000000U;
    e1000_write(E1000_REG_RAL, ral);
    e1000_write(E1000_REG_RAH, rah);
    device.rx_descriptor_physical = pmm_alloc_frame();
    device.tx_descriptor_physical = pmm_alloc_frame();
    if (device.rx_descriptor_physical == 0 || device.tx_descriptor_physical == 0) {
        return -1;
    }
    device.rx_descriptors = (struct e1000_rx_descriptor *)(usize)device.rx_descriptor_physical;
    device.tx_descriptors = (struct e1000_tx_descriptor *)(usize)device.tx_descriptor_physical;
    memset(device.rx_descriptors, 0, PAGE_SIZE);
    memset(device.tx_descriptors, 0, PAGE_SIZE);
    device.frames = kzalloc(sizeof(struct e1000_frame) * E1000_FRAME_QUEUE_DEPTH);
    if (device.frames == NULL) {
        return -1;
    }
    for (u32 index = 0; index < E1000_RX_DESCRIPTORS; index++) {
        u64 frame = pmm_alloc_frame();
        if (frame == 0) {
            return -1;
        }
        device.rx_buffer_physical[index] = frame;
        device.rx_descriptors[index].address = frame;
        device.rx_descriptors[index].status = 0;
    }
    for (u32 index = 0; index < E1000_TX_DESCRIPTORS; index++) {
        u64 frame = pmm_alloc_frame();
        if (frame == 0) {
            return -1;
        }
        device.tx_buffer_physical[index] = frame;
        device.tx_descriptors[index].address = frame;
        device.tx_descriptors[index].status = E1000_TXD_STATUS_DD;
    }
    device.rx_descriptor_count = E1000_RX_DESCRIPTORS;
    device.tx_descriptor_count = E1000_TX_DESCRIPTORS;
    e1000_write(E1000_REG_RDBAL, (u32)device.rx_descriptor_physical);
    e1000_write(E1000_REG_RDBAH, (u32)(device.rx_descriptor_physical >> 32));
    e1000_write(E1000_REG_RDLEN, E1000_RX_DESCRIPTORS * sizeof(struct e1000_rx_descriptor));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, E1000_RX_DESCRIPTORS - 1);
    device.rx_tail = 0;
    e1000_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
                                E1000_RCTL_SECRC);
    e1000_write(E1000_REG_TDBAL, (u32)device.tx_descriptor_physical);
    e1000_write(E1000_REG_TDBAH, (u32)(device.tx_descriptor_physical >> 32));
    e1000_write(E1000_REG_TDLEN, E1000_TX_DESCRIPTORS * sizeof(struct e1000_tx_descriptor));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);
    device.tx_tail = 0;
    e1000_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT_15 |
                                E1000_TCTL_COLD_FD);
    e1000_write(E1000_REG_TIPG, 0x0060200A);
    e1000_write(E1000_REG_ICR, 0xFFFFFFFFU);
    e1000_write(E1000_REG_IMS, E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_LSC | E1000_ICR_TXDW);
    if (device.irq < 16) {
        pic_set_mask(device.irq, false);
    }
    device.present = true;
    device.initialized = true;
    return 0;
}

bool e1000_present(void) {
    return device.present;
}

const struct e1000_device *e1000_device_state(void) {
    return &device;
}

u8 e1000_irq_line(void) {
    return device.irq;
}

void e1000_get_mac(u8 *mac) {
    if (mac == NULL) {
        return;
    }
    memcpy(mac, device.mac, 6);
}

const char *e1000_link_state(void) {
    if (!device.initialized) {
        return "down";
    }
    return (e1000_read(E1000_REG_STATUS) & E1000_STATUS_LU) != 0 ? "up" : "down";
}

int e1000_transmit(const void *frame, u32 length) {
    if (!device.initialized || frame == NULL || length == 0 || length > E1000_BUFFER_SIZE) {
        return -1;
    }
    u32 descriptor_index = device.tx_tail;
    struct e1000_tx_descriptor *descriptor = &device.tx_descriptors[descriptor_index];
    for (u32 spin = 0; spin < 1000000; spin++) {
        if ((descriptor->status & E1000_TXD_STATUS_DD) != 0) {
            break;
        }
    }
    u64 buffer_physical = device.tx_buffer_physical[descriptor_index];
    memcpy((void *)(usize)buffer_physical, frame, length);
    descriptor->address = buffer_physical;
    descriptor->length = (u16)length;
    descriptor->checksum_offset = 0;
    descriptor->checksum_start = 0;
    descriptor->special = 0;
    descriptor->command = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    descriptor->status = 0;
    device.tx_tail = (device.tx_tail + 1) % E1000_TX_DESCRIPTORS;
    e1000_write(E1000_REG_TDT, device.tx_tail);
    device.packets_sent++;
    return 0;
}

void e1000_poll(void) {
    if (!device.initialized) {
        return;
    }
    for (u32 spin = 0; spin < 32; spin++) {
        struct e1000_rx_descriptor *descriptor = &device.rx_descriptors[device.rx_tail];
        if ((descriptor->status & E1000_RXD_STATUS_DD) == 0) {
            break;
        }
        u32 length = descriptor->length;
        descriptor->status = 0;
        e1000_write(E1000_REG_RDT, device.rx_tail);
        if (length > 0 && length <= E1000_BUFFER_SIZE &&
            device.frame_count < E1000_FRAME_QUEUE_DEPTH) {
            struct e1000_frame *frame = &device.frames[device.frame_head];
            if (length > sizeof(frame->data)) {
                length = sizeof(frame->data);
            }
            frame->length = (u16)length;
            memcpy(frame->data, (const void *)(usize)descriptor->address, length);
            device.frame_head = (device.frame_head + 1) % E1000_FRAME_QUEUE_DEPTH;
            device.frame_count++;
            device.packets_received++;
        }
        device.rx_tail = (device.rx_tail + 1) % E1000_RX_DESCRIPTORS;
    }
}

u32 e1000_pending_frames(void) {
    return device.frame_count;
}

u32 e1000_dequeue_frame(void *buffer, u32 capacity) {
    if (buffer == NULL || device.frame_count == 0) {
        return 0;
    }
    struct e1000_frame *frame = &device.frames[device.frame_tail];
    u32 length = frame->length;
    if (length > capacity) {
        length = capacity;
    }
    memcpy(buffer, frame->data, length);
    device.frame_tail = (device.frame_tail + 1) % E1000_FRAME_QUEUE_DEPTH;
    device.frame_count--;
    return length;
}

u32 e1000_receive(void *buffer, u32 capacity) {
    e1000_poll();
    return e1000_dequeue_frame(buffer, capacity);
}

void e1000_irq_handler(void) {
    if (!device.initialized) {
        return;
    }
    u32 cause = e1000_read(E1000_REG_ICR);
    e1000_write(E1000_REG_ICR, cause);
    if ((cause & (E1000_ICR_RXT0 | E1000_ICR_RXDMT0)) != 0) {
        e1000_poll();
    }
}
