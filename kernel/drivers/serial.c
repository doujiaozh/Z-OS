#include "serial.h"
#include "io.h"
#include "input.h"

#define COM1 0x3F8

static bool serial_present = false;

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0x07);
    outb(COM1 + 4, 0x0B);
    serial_present = true;
}

void serial_putc(char character) {
    if (!serial_present) {
        return;
    }
    for (u32 spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (u8)character);
            return;
        }
    }
}

void serial_write(const char *text) {
    while (*text != 0) {
        serial_putc(*text++);
    }
}

bool serial_rx_ready(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

char serial_getc(void) {
    while (!serial_rx_ready()) {
    }
    return (char)inb(COM1);
}

void serial_enable_rx_interrupt(void) {
    outb(COM1 + 1, 0x01);
}

void serial_irq_handler(void) {
    while (serial_rx_ready()) {
        char character = (char)inb(COM1);
        if (character == '\r') {
            character = '\n';
        }
        input_push(character);
    }
}
