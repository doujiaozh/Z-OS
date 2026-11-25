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

struct mouse_state {
    i32 x;
    i32 y;
    u32 buttons;
    u32 packets;
    u32 errors;
    bool present;
    bool initialized;
};

void mouse_init(void);
void mouse_process_packet(const u8 *packet);
void mouse_irq_handler(void);
void mouse_get_state(struct mouse_state *state);
void mouse_set_bounds(u32 width, u32 height);
void mouse_move_to(i32 x, i32 y);
void mouse_move_by(i32 dx, i32 dy);
int mouse_render(struct gfx_surface *surface);
int mouse_restore(struct gfx_surface *surface);
u32 mouse_packet_count(void);

#endif
