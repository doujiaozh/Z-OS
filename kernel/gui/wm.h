#ifndef Z_OS_WM_H
#define Z_OS_WM_H

#include "gfx.h"
#include "types.h"

#define WM_MAX_WINDOWS 8
#define WM_MAX_WIDTH 512
#define WM_MAX_HEIGHT 384

struct wm_window {
    bool used;
    u32 id;
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 background;
    bool visible;
    bool owns_surface;
    struct gfx_surface surface;
};

void wm_init(void);
struct wm_window *wm_create(i32 x, i32 y, u32 width, u32 height, u32 background);
int wm_destroy(u32 id);
struct wm_window *wm_find(u32 id);
struct wm_window *wm_window_at(u32 index);
u32 wm_count(void);
int wm_fill(u32 id, i32 x, i32 y, u32 width, u32 height, u32 color);
int wm_clear(u32 id, u32 color);
int wm_draw_to(struct gfx_surface *target);
u32 wm_zorder_at(u32 index);

#endif
