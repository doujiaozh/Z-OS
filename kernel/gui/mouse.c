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
#define PS2_COMMAND_ENABLE_AUX 0xA8
#define PS2_COMMAND_READ_CONFIG 0x20
#define PS2_COMMAND_WRITE_CONFIG 0x60
#define PS2_COMMAND_WRITE_AUX 0xD4
#define PS2_ACK 0xFA
#define PS2_DEVICE_SET_DEFAULTS 0xF6
#define PS2_DEVICE_ENABLE_REPORTING 0xF4
#define PS2_CONFIG_AUX_IRQ 0x02
#define PS2_CONFIG_AUX_CLOCK 0x20

#define MOUSE_CURSOR_WIDTH 16
#define MOUSE_CURSOR_HEIGHT 16
#define MOUSE_CURSOR_HOTSPOT_X 0
#define MOUSE_CURSOR_HOTSPOT_Y 0
#define MOUSE_CURSOR_OUTLINE_COLOR 0x00000000U
#define MOUSE_CURSOR_FILL_COLOR 0x00FFFFFFU

static const u16 cursor_outline[MOUSE_CURSOR_HEIGHT] = {
    0x8000, 0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8200, 0x8100,
    0x8080, 0x8440, 0x8A20, 0x9110, 0xA088, 0xC048, 0x8028, 0x0010
};

static const u16 cursor_fill[MOUSE_CURSOR_HEIGHT] = {
    0x0000, 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00,
    0x7F00, 0x7B80, 0x71C0, 0x60E0, 0x4070, 0x0030, 0x0010, 0x0000
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
static struct mouse_event event_queue[MOUSE_QUEUE_SIZE];
static u32 event_head = 0;
static u32 event_tail = 0;
static u32 event_count = 0;
static bool gui_mode = false;
static bool text_cursor_drawn = false;
static i32 text_cursor_column = -1;
static i32 text_cursor_row = -1;
static u16 text_cursor_saved[2][2];

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

static void ps2_flush_output(void) {
    for (u32 spin = 0; spin < 64; spin++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) == 0) {
            return;
        }
        inb(PS2_DATA_PORT);
    }
}

static bool ps2_write_aux(u8 value) {
    ps2_write_command(PS2_COMMAND_WRITE_AUX);
    ps2_write_data(value);
    u8 response = ps2_read_data();
    for (u32 spin = 0; spin < 8 && response != PS2_ACK; spin++) {
        response = ps2_read_data();
    }
    return response == PS2_ACK;
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
    ps2_flush_output();
    ps2_write_command(PS2_COMMAND_ENABLE_AUX);
    ps2_write_command(PS2_COMMAND_READ_CONFIG);
    u8 configuration = ps2_read_data();
    configuration |= PS2_CONFIG_AUX_IRQ;
    configuration &= (u8)~PS2_CONFIG_AUX_CLOCK;
    ps2_write_command(PS2_COMMAND_WRITE_CONFIG);
    ps2_write_data(configuration);
    ps2_write_command(PS2_COMMAND_ENABLE_AUX);
    bool defaults_ok = ps2_write_aux(PS2_DEVICE_SET_DEFAULTS);
    bool reporting_ok = ps2_write_aux(PS2_DEVICE_ENABLE_REPORTING);
    ps2_flush_output();
    pic_set_mask(PIC_IRQ_CASCADE, false);
    pic_set_mask(12, false);
    state.present = defaults_ok && reporting_ok;
    state.initialized = true;
}

void mouse_event_init(void) {
    mouse_move_to(752, 568);
    event_head = 0;
    event_tail = 0;
    event_count = 0;
    gui_mode = false;
    text_cursor_drawn = false;
    text_cursor_column = -1;
    text_cursor_row = -1;
}

void mouse_inject_event(u8 type, i32 x, i32 y, i32 delta_x, i32 delta_y, u32 buttons) {
    u32 next = (event_head + 1) % MOUSE_QUEUE_SIZE;
    if (next == event_tail) {
        event_tail = (event_tail + 1) % MOUSE_QUEUE_SIZE;
        if (event_count > 0) {
            event_count--;
        }
    }
    struct mouse_event *event = &event_queue[event_head];
    event->type = type;
    event->x = x;
    event->y = y;
    event->delta_x = delta_x;
    event->delta_y = delta_y;
    event->buttons = buttons;
    event_head = next;
    event_count++;
}

bool mouse_pop_event(struct mouse_event *event) {
    if (event == NULL || event_count == 0 || event_tail == event_head) {
        return false;
    }
    *event = event_queue[event_tail];
    event_tail = (event_tail + 1) % MOUSE_QUEUE_SIZE;
    event_count--;
    return true;
}

u32 mouse_event_count(void) {
    return event_count;
}

void mouse_set_gui_mode(bool enabled) {
    gui_mode = enabled;
    if (enabled) {
        mouse_text_cursor_restore();
    }
}

bool mouse_gui_mode(void) {
    return gui_mode;
}

static volatile u16 *text_vga(void) {
    return (volatile u16 *)0xB8000;
}

bool mouse_text_cursor_visible(void) {
    return text_cursor_drawn;
}

i32 mouse_text_cursor_column(void) {
    return text_cursor_column;
}

i32 mouse_text_cursor_row(void) {
    return text_cursor_row;
}

void mouse_text_cursor_restore(void) {
    if (!text_cursor_drawn) {
        return;
    }
    for (i32 row = 0; row < 2; row++) {
        for (i32 column = 0; column < 2; column++) {
            i32 cell_column = text_cursor_column + column;
            i32 cell_row = text_cursor_row + row;
            if (cell_column < 0 || cell_column >= 80 || cell_row < 0 || cell_row >= 25) {
                continue;
            }
            text_vga()[(u32)cell_row * 80 + (u32)cell_column] = text_cursor_saved[row][column];
        }
    }
    text_cursor_drawn = false;
}

void mouse_text_cursor_update(void) {
    if (gui_mode) {
        return;
    }
    i32 column = state.x / 8;
    i32 row = state.y / 16;
    if (column < 0) {
        column = 0;
    }
    if (row < 0) {
        row = 0;
    }
    if (column > 78) {
        column = 78;
    }
    if (row > 23) {
        row = 23;
    }
    if (text_cursor_drawn && column == text_cursor_column && row == text_cursor_row) {
        return;
    }
    mouse_text_cursor_restore();
    text_cursor_column = column;
    text_cursor_row = row;
    for (i32 local_row = 0; local_row < 2; local_row++) {
        for (i32 local_column = 0; local_column < 2; local_column++) {
            i32 cell_column = column + local_column;
            i32 cell_row = row + local_row;
            text_cursor_saved[local_row][local_column] = text_vga()[(u32)cell_row * 80 + (u32)cell_column];
            text_vga()[(u32)cell_row * 80 + (u32)cell_column] = (u16)(0x4F00 | (u16)'\xDB');
        }
    }
    text_cursor_drawn = true;
}

void mouse_process_packet(const u8 *packet) {
    if (packet == NULL) {
        return;
    }
    if ((packet[0] & 0x08) == 0) {
        state.errors++;
        return;
    }
    i32 delta_x = (i32)(u8)packet[1];
    i32 delta_y = (i32)(u8)packet[2];
    if ((packet[0] & 0x10) != 0) {
        delta_x |= 0xFFFFFF00;
    }
    if ((packet[0] & 0x20) != 0) {
        delta_y |= 0xFFFFFF00;
    }
    u32 previous_buttons = state.buttons;
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
    if (delta_x != 0 || delta_y != 0) {
        mouse_inject_event(MOUSE_EVENT_MOVE, state.x, state.y, delta_x, -delta_y, state.buttons);
    }
    if (state.buttons != previous_buttons) {
        mouse_inject_event(MOUSE_EVENT_BUTTON, state.x, state.y, 0, 0, state.buttons);
    }
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
            i32 x = saved_x + (i32)column;
            i32 y = saved_y + (i32)row;
            if (x < 0 || y < 0 || x >= (i32)surface->width || y >= (i32)surface->height) {
                continue;
            }
            gfx_plot(surface, x, y, saved_pixels[row * MOUSE_CURSOR_WIDTH + column]);
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
        i32 y = origin_y + (i32)row;
        if (y < 0 || y >= (i32)surface->height) {
            continue;
        }
        for (u32 column = 0; column < MOUSE_CURSOR_WIDTH; column++) {
            i32 x = origin_x + (i32)column;
            if (x < 0 || x >= (i32)surface->width) {
                continue;
            }
            saved_pixels[row * MOUSE_CURSOR_WIDTH + column] = gfx_get_pixel(surface, x, y);
            u16 bit = (u16)(0x8000U >> column);
            if ((cursor_outline[row] & bit) != 0) {
                gfx_plot(surface, x, y, MOUSE_CURSOR_OUTLINE_COLOR);
            } else if ((cursor_fill[row] & bit) != 0) {
                gfx_plot(surface, x, y, MOUSE_CURSOR_FILL_COLOR);
            }
        }
    }
    cursor_saved = true;
    return 0;
}
