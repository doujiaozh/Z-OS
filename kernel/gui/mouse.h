#ifndef Z_OS_MOUSE_H
#define Z_OS_MOUSE_H

#include "gfx.h"
#include "types.h"

#define MOUSE_PACKET_SIZE 3
#define MOUSE_DEFAULT_WIDTH 800
#define MOUSE_DEFAULT_HEIGHT 600
#define MOUSE_BUTTON_LEFT 0x1
#define MOUSE_BUTTON_RIGHT 0x2
#define MOUSE_BUTTON_MIDDLE 0x4

#define MOUSE_EVENT_MOVE 1
#define MOUSE_EVENT_BUTTON 2
#define MOUSE_QUEUE_SIZE 32

struct mouse_state {
    i32 x;
    i32 y;
    u32 buttons;
    u32 packets;
    u32 errors;
    bool present;
    bool initialized;
};

struct mouse_event {
    u8 type;
    u8 reserved[3];
    i32 x;
    i32 y;
    i32 delta_x;
    i32 delta_y;
    u32 buttons;
};

void mouse_init(void);
void mouse_event_init(void);
void mouse_process_packet(const u8 *packet);
void mouse_irq_handler(void);
void mouse_get_state(struct mouse_state *state);
void mouse_set_bounds(u32 width, u32 height);
void mouse_move_to(i32 x, i32 y);
void mouse_move_by(i32 dx, i32 dy);
int mouse_render(struct gfx_surface *surface);
int mouse_restore(struct gfx_surface *surface);
u32 mouse_packet_count(void);
bool mouse_pop_event(struct mouse_event *event);
u32 mouse_event_count(void);
void mouse_inject_event(u8 type, i32 x, i32 y, i32 delta_x, i32 delta_y, u32 buttons);
void mouse_set_gui_mode(bool enabled);
bool mouse_gui_mode(void);
void mouse_text_cursor_update(void);
void mouse_text_cursor_restore(void);
bool mouse_text_cursor_visible(void);
i32 mouse_text_cursor_column(void);
i32 mouse_text_cursor_row(void);

#endif
