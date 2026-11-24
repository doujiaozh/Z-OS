#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

void pic_remap(void) {
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_set_mask(u8 irq, bool masked) {
    u16 port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1 << (irq & 7));
    u8 value = inb(port);
    if (masked) {
        value |= bit;
    } else {
        value &= (u8)~bit;
    }
    outb(port, value);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}
