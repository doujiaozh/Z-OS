#ifndef Z_OS_DESKTOP_H
#define Z_OS_DESKTOP_H

#include "gfx.h"
#include "mouse.h"
#include "types.h"
#include "wm.h"

#define DESKTOP_STATUS_HEIGHT 28
#define DESKTOP_TASKBAR_HEIGHT 36
#define DESKTOP_ICON_COUNT 4
#define DESKTOP_ICON_WIDTH 88
#define DESKTOP_ICON_HEIGHT 74
#define DESKTOP_MAX_APPS 4

#define DESKTOP_COLOR_WALLPAPER 0x00101820U
#define DESKTOP_COLOR_STATUS 0x00203040U
#define DESKTOP_COLOR_TASKBAR 0x001B2430U
#define DESKTOP_COLOR_ICON_BODY 0x002F4152U
#define DESKTOP_COLOR_ICON_TEXT 0x00E8EEF7U
#define DESKTOP_COLOR_WINDOW_BODY 0x00F0F4FAU

struct desktop_app {
    const char *name;
    const char *title;
    u32 width;
    u32 height;
    u32 id;
    bool open;
    bool visible;
};

void desktop_init(struct gfx_surface *surface);
void desktop_set_surface(struct gfx_surface *surface);
struct gfx_surface *desktop_surface(void);
void desktop_render(void);
bool desktop_handle_mouse(const struct mouse_event *event);
bool desktop_handle_key(char key);
void desktop_request_exit(void);
void desktop_dirty_reset(void);
void desktop_dirty_mark(i32 x, i32 y, u32 width, u32 height);
void desktop_dirty_mark_full(void);
bool desktop_dirty_take(i32 *x, i32 *y, u32 *width, u32 *height);
bool desktop_should_exit(void);
u32 desktop_window_count(void);
u32 desktop_click_count(void);
const char *desktop_last_action(void);
bool desktop_icon_at(i32 x, i32 y, u32 *index);
u32 desktop_app_id(u32 index);
bool desktop_app_is_open(u32 index);
int desktop_open_app(u32 index);
int desktop_close_app(u32 index);
void desktop_open_window_for_test(u32 index);
i32 desktop_dragging_id(void);
u64 desktop_render_count(void);
u32 desktop_status_bar_pixel(void);
u32 desktop_taskbar_pixel(void);
u32 desktop_wallpaper_pixel(void);
bool desktop_taskbar_exit_hit(i32 x, i32 y);
bool desktop_taskbar_button_hit(i32 x, i32 y, u32 *index);

#endif
