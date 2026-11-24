#include "console.h"
#include "serial.h"
#include "io.h"
#include "string.h"
#include "thread.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile u16 *vga = (volatile u16 *)VGA_ADDRESS;
static u32 cursor_row = 0;
static u32 cursor_column = 0;
static u8 current_color = 0x07;

static u16 vga_entry(char character, u8 color) {
    return (u16)(u8)character | ((u16)color << 8);
}

static void console_update_cursor(void) {
    u16 position = (u16)(cursor_row * VGA_WIDTH + cursor_column);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((position >> 8) & 0xFF));
}

static void console_scroll(void) {
    for (u32 row = 1; row < VGA_HEIGHT; row++) {
        for (u32 column = 0; column < VGA_WIDTH; column++) {
            vga[(row - 1) * VGA_WIDTH + column] = vga[row * VGA_WIDTH + column];
        }
    }
    for (u32 column = 0; column < VGA_WIDTH; column++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + column] = vga_entry(' ', current_color);
    }
    cursor_row = VGA_HEIGHT - 1;
}

static void console_putc_unlocked(char character) {
    if (character == '\n') {
        serial_putc('\r');
        serial_putc('\n');
    } else {
        serial_putc(character);
    }
    if (character == '\n') {
        cursor_column = 0;
        cursor_row++;
    } else if (character == '\r') {
        cursor_column = 0;
    } else if (character == '\b') {
        if (cursor_column > 0) {
            cursor_column--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_column = VGA_WIDTH - 1;
        }
        vga[cursor_row * VGA_WIDTH + cursor_column] = vga_entry(' ', current_color);
    } else if (character == '\t') {
        u32 spaces = 4 - (cursor_column % 4);
        for (u32 index = 0; index < spaces; index++) {
            console_putc_unlocked(' ');
        }
        return;
    } else {
        vga[cursor_row * VGA_WIDTH + cursor_column] = vga_entry(character, current_color);
        cursor_column++;
    }
    if (cursor_column >= VGA_WIDTH) {
        cursor_column = 0;
        cursor_row++;
    }
    while (cursor_row >= VGA_HEIGHT) {
        console_scroll();
    }
    console_update_cursor();
}

void console_init(void) {
    current_color = 0x07;
    cursor_row = 0;
    cursor_column = 0;
    for (u32 index = 0; index < VGA_WIDTH * VGA_HEIGHT; index++) {
        vga[index] = vga_entry(' ', current_color);
    }
    console_update_cursor();
}

void console_clear(void) {
    sched_preempt_disable();
    for (u32 index = 0; index < VGA_WIDTH * VGA_HEIGHT; index++) {
        vga[index] = vga_entry(' ', current_color);
    }
    cursor_row = 0;
    cursor_column = 0;
    console_update_cursor();
    sched_preempt_enable();
}

void console_putc(char character) {
    sched_preempt_disable();
    console_putc_unlocked(character);
    sched_preempt_enable();
}

void console_write(const char *text) {
    sched_preempt_disable();
    while (*text != 0) {
        console_putc_unlocked(*text++);
    }
    sched_preempt_enable();
}

void console_write_n(const char *text, usize count) {
    sched_preempt_disable();
    for (usize index = 0; index < count; index++) {
        console_putc_unlocked(text[index]);
    }
    sched_preempt_enable();
}

void console_write_line(const char *text) {
    console_write(text);
    console_putc('\n');
}

void console_set_color(u8 foreground, u8 background) {
    current_color = (u8)((background << 4) | (foreground & 0x0F));
}

void console_reset_color(void) {
    current_color = 0x07;
}

void console_backspace(void) {
    console_putc('\b');
}

u32 console_columns(void) {
    return VGA_WIDTH;
}

u32 console_rows(void) {
    return VGA_HEIGHT;
}
