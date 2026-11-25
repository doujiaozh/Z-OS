#include "mouse.h"
#include "io.h"
#include "lib.h"
#include "pic.h"
#include "string.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_STATUS_OUTPUT 0x01
#define PS2_STATUS_INPUT 0x02
#define MOUSE_CURSOR_WIDTH 12
#define MOUSE_CURSOR_HEIGHT 12
#define MOUSE_CURSOR_HOTSPOT_X 1
#define MOUSE_CURSOR_HOTSPOT_Y 1

static const u8 cursor_mask[MOUSE_CURSOR_HEIGHT] = {
    0xC0, 0xF0, 0xFC, 0xFF, 0xFF, 0xFF, 0xFC, 0xF8, 0xD8, 0x88, 0x08, 0x00
};

static struct mouse_state state;
static u8 packet_buffer[MOUSE_PACKET_SIZE];
static u32 packet_index = 0;
static u32 screen_width = MOUSE_DEFAULT_WIDTH;
static u32 screen_height = MOUSE_DEFAULT_HEIGHT;
static u32 saved_pixels[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT];
static bool cursor_saved = false;
static i32 saved_x = 0;
static i32 saved_y = 0;

static void ps2_wait_input(void) {
    for (u32 spin = 0; spin < 100000; spin++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) == 0) {
            return;
        }
    }
}

static void ps2_wait_output(void) {
    for (u32 spin = 0; spin < 100000; spin++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) != 0) {
            return;
        }
    }
}

static void ps2_write_command(u8 command) {
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, command);
}

static void ps2_write_data(u8 value) {
    ps2_wait_input();
    outb(PS2_DATA_PORT, value);
}

static u8 ps2_read_data(void) {
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

void mouse_set_bounds(u32 width, u32 height) {
    screen_width = width == 0 ? MOUSE_DEFAULT_WIDTH : width;
    screen_height = height == 0 ? MOUSE_DEFAULT_HEIGHT : height;
    if (state.x >= (i32)screen_width) {
        state.x = (i32)screen_width - 1;
    }
    if (state.y >= (i32)screen_height) {
        state.y = (i32)screen_height - 1;
    }
}

void mouse_init(void) {
    memset(&state, 0, sizeof(state));
    state.x = (i32)(screen_width / 2);
    state.y = (i32)(screen_height / 2);
    packet_index = 0;
    cursor_saved = false;
    ps2_write_command(0xA8);
    ps2_write_command(0x20);
    u8 configuration = ps2_read_data();
    configuration |= 0x02;
    configuration &= (u8)~0x20;
    ps2_write_command(0x60);
    ps2_write_data(configuration);
    ps2_write_command(0xA8);
    ps2_write_data(0xF6);
    ps2_read_data();
    ps2_write_data(0xF4);
    u8 acknowledgement = ps2_read_data();
    pic_set_mask(PIC_IRQ_CASCADE, false);
    pic_set_mask(12, false);
    state.present = acknowledgement == 0xFA;
    state.initialized = true;
}

void mouse_process_packet(const u8 *packet) {
    if (packet == NULL) {
        return;
    }
    if ((packet[0] & 0x08) == 0) {
        state.errors++;
        return;
    }
    i32 delta_x = packet[1];
    i32 delta_y = packet[2];
    if ((packet[0] & 0x10) != 0) {
        delta_x |= 0xFFFFFF00;
    }
    if ((packet[0] & 0x20) != 0) {
        delta_y |= 0xFFFFFF00;
    }
    state.buttons = packet[0] & 0x07;
    state.x += delta_x;
    state.y -= delta_y;
    if (state.x < 0) {
        state.x = 0;
    }
    if (state.y < 0) {
        state.y = 0;
    }
    if (state.x >= (i32)screen_width) {
        state.x = (i32)screen_width - 1;
    }
    if (state.y >= (i32)screen_height) {
        state.y = (i32)screen_height - 1;
    }
    state.packets++;
}

void mouse_irq_handler(void) {
    u8 value = inb(PS2_DATA_PORT);
    if (packet_index == 0 && (value & 0x08) == 0) {
        state.errors++;
        return;
    }
    packet_buffer[packet_index++] = value;
    if (packet_index >= MOUSE_PACKET_SIZE) {
        packet_index = 0;
        mouse_process_packet(packet_buffer);
    }
}

void mouse_get_state(struct mouse_state *destination) {
    if (destination == NULL) {
        return;
    }
    *destination = state;
}

void mouse_move_to(i32 x, i32 y) {
    state.x = x;
    state.y = y;
    if (state.x < 0) {
        state.x = 0;
    }
    if (state.y < 0) {
        state.y = 0;
    }
    if (state.x >= (i32)screen_width) {
        state.x = (i32)screen_width - 1;
    }
    if (state.y >= (i32)screen_height) {
        state.y = (i32)screen_height - 1;
    }
}

void mouse_move_by(i32 dx, i32 dy) {
    mouse_move_to(state.x + dx, state.y + dy);
}

u32 mouse_packet_count(void) {
    return state.packets;
}

static i32 cursor_origin_x(void) {
    return state.x - MOUSE_CURSOR_HOTSPOT_X;
}

static i32 cursor_origin_y(void) {
    return state.y - MOUSE_CURSOR_HOTSPOT_Y;
}

int mouse_restore(struct gfx_surface *surface) {
    if (surface == NULL || !surface->valid || !cursor_saved) {
        return -1;
    }
    for (u32 row = 0; row < MOUSE_CURSOR_HEIGHT; row++) {
        for (u32 column = 0; column < MOUSE_CURSOR_WIDTH; column++) {
            if ((cursor_mask[row] & (0x80U >> column)) == 0) {
                continue;
            }
            gfx_plot(surface, saved_x + (i32)column, saved_y + (i32)row,
                     saved_pixels[row * MOUSE_CURSOR_WIDTH + column]);
        }
    }
    cursor_saved = false;
    return 0;
}

int mouse_render(struct gfx_surface *surface) {
    if (surface == NULL || !surface->valid) {
        return -1;
    }
    mouse_restore(surface);
    i32 origin_x = cursor_origin_x();
    i32 origin_y = cursor_origin_y();
    saved_x = origin_x;
    saved_y = origin_y;
    for (u32 row = 0; row < MOUSE_CURSOR_HEIGHT; row++) {
        for (u32 column = 0; column < MOUSE_CURSOR_WIDTH; column++) {
            if ((cursor_mask[row] & (0x80U >> column)) == 0) {
                continue;
            }
            i32 x = origin_x + (i32)column;
            i32 y = origin_y + (i32)row;
            saved_pixels[row * MOUSE_CURSOR_WIDTH + column] = gfx_get_pixel(surface, x, y);
            gfx_plot(surface, x, y, 0x00FFFFFFU);
        }
    }
    cursor_saved = true;
    return 0;
}
