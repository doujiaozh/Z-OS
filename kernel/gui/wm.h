#ifndef Z_OS_WM_H
#define Z_OS_WM_H

#include "gfx.h"
#include "types.h"

#define WM_MAX_WINDOWS 8
#define WM_MAX_WIDTH 512
#define WM_MAX_HEIGHT 384
#define WM_TITLE_HEIGHT 18
#define WM_CLOSE_SIZE 12

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
    char title[32];
    bool has_title;
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
void wm_set_title(struct wm_window *window, const char *title);
bool wm_point_in_window(const struct wm_window *window, i32 x, i32 y);
bool wm_title_hit(const struct wm_window *window, i32 x, i32 y);
bool wm_close_hit(const struct wm_window *window, i32 x, i32 y);
struct wm_window *wm_window_at_point(i32 x, i32 y);
void wm_raise(u32 id);
void wm_set_visible(u32 id, bool visible);
void wm_move_to(u32 id, i32 x, i32 y);
void wm_draw_frame(struct gfx_surface *target, const struct wm_window *window);
int wm_compose(struct gfx_surface *target);

#endif
