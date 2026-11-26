#include "desktop.h"
#include "font.h"
#include "lib.h"
#include "string.h"
#include "timer.h"

static struct gfx_surface *desktop_target = NULL;
static struct desktop_app apps[DESKTOP_MAX_APPS];
static bool exit_requested = false;
static u32 click_count = 0;
static char last_action[48];
static u64 render_count = 0;
static i32 dragging_id = 0;
static i32 drag_offset_x = 0;
static i32 drag_offset_y = 0;
static char key_buffer[16];
static u32 key_length = 0;
static u32 taskbar_buttons[DESKTOP_MAX_APPS];
static bool dirty_valid = false;
static i32 dirty_x = 0;
static i32 dirty_y = 0;
static u32 dirty_width = 0;
static u32 dirty_height = 0;

static void dirty_union(i32 x, i32 y, u32 width, u32 height) {
    if (x < 0) {
        width += (u32)(-x);
        x = 0;
    }
    if (y < 0) {
        height += (u32)(-y);
        y = 0;
    }
    if ((u32)x >= 800 || (u32)y >= 600 || width == 0 || height == 0) {
        return;
    }
    if ((u32)x + width > 800) {
        width = 800 - (u32)x;
    }
    if ((u32)y + height > 600) {
        height = 600 - (u32)y;
    }
    if (!dirty_valid) {
        dirty_valid = true;
        dirty_x = x;
        dirty_y = y;
        dirty_width = width;
        dirty_height = height;
        return;
    }
    i32 right = x + (i32)width;
    i32 bottom = y + (i32)height;
    i32 current_right = dirty_x + (i32)dirty_width;
    i32 current_bottom = dirty_y + (i32)dirty_height;
    if (x < dirty_x) {
        dirty_x = x;
    }
    if (y < dirty_y) {
        dirty_y = y;
    }
    if (right > current_right) {
        current_right = right;
    }
    if (bottom > current_bottom) {
        current_bottom = bottom;
    }
    dirty_width = (u32)(current_right - dirty_x);
    dirty_height = (u32)(current_bottom - dirty_y);
}

void desktop_dirty_reset(void) {
    dirty_valid = false;
}

void desktop_dirty_mark(i32 x, i32 y, u32 width, u32 height) {
    dirty_union(x, y, width, height);
}

void desktop_dirty_mark_full(void) {
    dirty_valid = true;
    dirty_x = 0;
    dirty_y = 0;
    dirty_width = 800;
    dirty_height = 600;
}

bool desktop_dirty_take(i32 *x, i32 *y, u32 *width, u32 *height) {
    if (!dirty_valid) {
        return false;
    }
    if (x != NULL) {
        *x = dirty_x;
    }
    if (y != NULL) {
        *y = dirty_y;
    }
    if (width != NULL) {
        *width = dirty_width;
    }
    if (height != NULL) {
        *height = dirty_height;
    }
    dirty_valid = false;
    return true;
}

static void set_action(const char *text) {
    strncpy(last_action, text, sizeof(last_action) - 1);
    last_action[sizeof(last_action) - 1] = 0;
}

static void icon_rect(u32 index, i32 *x, i32 *y) {
    *x = 20;
    *y = DESKTOP_STATUS_HEIGHT + 18 + (i32)index * (DESKTOP_ICON_HEIGHT + 10);
}

static void taskbar_button_rect(u32 index, i32 *x, i32 *y, u32 *width) {
    *x = 96 + (i32)index * 132;
    *y = 600 - DESKTOP_TASKBAR_HEIGHT + 6;
    *width = 124;
}

static void draw_icon(u32 index) {
    i32 x = 0;
    i32 y = 0;
    icon_rect(index, &x, &y);
    u32 body = 0x00D9453DU;
    if (index == 1) {
        body = 0x002E9E5BU;
    } else if (index == 2) {
        body = 0x002F6FB5U;
    } else if (index == 3) {
        body = 0x007B4FA0U;
    }
    gfx_fill_rect(desktop_target, x, y, DESKTOP_ICON_WIDTH, DESKTOP_ICON_HEIGHT - 20, DESKTOP_COLOR_ICON_BODY);
    gfx_fill_rect(desktop_target, x + 16, y + 8, DESKTOP_ICON_WIDTH - 32, DESKTOP_ICON_HEIGHT - 36, body);
    font_draw_text(desktop_target, x + 4, y + DESKTOP_ICON_HEIGHT - 17, apps[index].name, DESKTOP_COLOR_ICON_TEXT);
}

static void draw_status_bar(void) {
    gfx_fill_rect(desktop_target, 0, 0, 800, DESKTOP_STATUS_HEIGHT, DESKTOP_COLOR_STATUS);
    font_draw_text(desktop_target, 12, 6, "Z-OS 0.6 Desktop", DESKTOP_COLOR_ICON_TEXT);
    font_draw_text(desktop_target, 320, 6, "storage online", 0x00C8D4E4U);
    u64 seconds = timer_uptime_ms() / 1000;
    char clock_text[24];
    ksnprintf(clock_text, sizeof(clock_text), "uptime %llu s", seconds);
    font_draw_text(desktop_target, 660, 6, clock_text, DESKTOP_COLOR_ICON_TEXT);
}

static void draw_taskbar(void) {
    i32 top = 600 - DESKTOP_TASKBAR_HEIGHT;
    gfx_fill_rect(desktop_target, 0, top, 800, DESKTOP_TASKBAR_HEIGHT, DESKTOP_COLOR_TASKBAR);
    gfx_fill_rect(desktop_target, 8, top + 6, 76, DESKTOP_TASKBAR_HEIGHT - 12, 0x002F6FB5U);
    font_draw_text(desktop_target, 18, top + 10, "Z-OS", 0x00F4F8FFU);
    for (u32 index = 0; index < DESKTOP_MAX_APPS; index++) {
        i32 x = 0;
        i32 y = 0;
        u32 width = 0;
        taskbar_button_rect(index, &x, &y, &width);
        taskbar_buttons[index] = apps[index].open ? apps[index].id : 0;
        u32 color = apps[index].open ? 0x003A4C60U : 0x0026303CU;
        gfx_fill_rect(desktop_target, x, y, (i32)width, DESKTOP_TASKBAR_HEIGHT - 12, color);
        font_draw_text(desktop_target, x + 8, y + 4, apps[index].name, DESKTOP_COLOR_ICON_TEXT);
    }
    gfx_fill_rect(desktop_target, 700, top + 6, 92, DESKTOP_TASKBAR_HEIGHT - 12, 0x00C43B3BU);
    font_draw_text(desktop_target, 714, top + 10, "Exit", 0x00FFFFFFU);
}

void desktop_init(struct gfx_surface *surface) {
    desktop_target = surface;
    desktop_dirty_reset();
    exit_requested = false;
    click_count = 0;
    render_count = 0;
    dragging_id = 0;
    key_length = 0;
    memset(key_buffer, 0, sizeof(key_buffer));
    memset(last_action, 0, sizeof(last_action));
    memset(taskbar_buttons, 0, sizeof(taskbar_buttons));
    wm_init();
    apps[0].name = "Files";
    apps[0].title = "Files";
    apps[0].width = 360;
    apps[0].height = 240;
    apps[1].name = "Terminal";
    apps[1].title = "Terminal";
    apps[1].width = 380;
    apps[1].height = 220;
    apps[2].name = "Monitor";
    apps[2].title = "System Monitor";
    apps[2].width = 340;
    apps[2].height = 200;
    apps[3].name = "Exit";
    apps[3].title = "Exit";
    apps[3].width = 200;
    apps[3].height = 120;
    for (u32 index = 0; index < DESKTOP_MAX_APPS; index++) {
        apps[index].id = 0;
        apps[index].open = false;
        apps[index].visible = false;
    }
}

void desktop_set_surface(struct gfx_surface *surface) {
    desktop_target = surface;
}

struct gfx_surface *desktop_surface(void) {
    return desktop_target;
}

u32 desktop_status_bar_pixel(void) {
    return DESKTOP_COLOR_STATUS;
}

u32 desktop_taskbar_pixel(void) {
    return DESKTOP_COLOR_TASKBAR;
}

u32 desktop_wallpaper_pixel(void) {
    return DESKTOP_COLOR_WALLPAPER;
}

u32 desktop_window_count(void) {
    return wm_count();
}

u32 desktop_click_count(void) {
    return click_count;
}

const char *desktop_last_action(void) {
    return last_action;
}

u64 desktop_render_count(void) {
    return render_count;
}

i32 desktop_dragging_id(void) {
    return dragging_id;
}

u32 desktop_app_id(u32 index) {
    if (index >= DESKTOP_MAX_APPS) {
        return 0;
    }
    return apps[index].id;
}

bool desktop_app_is_open(u32 index) {
    return index < DESKTOP_MAX_APPS && apps[index].open;
}

bool desktop_icon_at(i32 x, i32 y, u32 *index) {
    for (u32 local = 0; local < DESKTOP_ICON_COUNT; local++) {
        i32 left = 0;
        i32 top = 0;
        icon_rect(local, &left, &top);
        if (x >= left && x < left + DESKTOP_ICON_WIDTH &&
            y >= top && y < top + DESKTOP_ICON_HEIGHT) {
            if (index != NULL) {
                *index = local;
            }
            return true;
        }
    }
    return false;
}

bool desktop_taskbar_exit_hit(i32 x, i32 y) {
    i32 top = 600 - DESKTOP_TASKBAR_HEIGHT;
    return x >= 700 && x < 792 && y >= top + 6 && y < top + DESKTOP_TASKBAR_HEIGHT - 6;
}

bool desktop_taskbar_button_hit(i32 x, i32 y, u32 *index) {
    for (u32 local = 0; local < DESKTOP_MAX_APPS; local++) {
        i32 left = 0;
        i32 top = 0;
        u32 width = 0;
        taskbar_button_rect(local, &left, &top, &width);
        if (x >= left && x < left + (i32)width && y >= top && y < top + DESKTOP_TASKBAR_HEIGHT - 12) {
            if (index != NULL) {
                *index = local;
            }
            return true;
        }
    }
    return false;
}

static void fill_window_body(struct wm_window *window) {
    if (window == NULL) {
        return;
    }
    gfx_clear(&window->surface, DESKTOP_COLOR_WINDOW_BODY);
}

static void paint_files_window(struct wm_window *window) {
    if (window == NULL) {
        return;
    }
    fill_window_body(window);
    gfx_fill_rect(&window->surface, 10, 34, 120, 20, 0x00DCE4EEU);
    font_draw_text(&window->surface, 14, 36, "/disk", 0x00203040U);
    font_draw_text(&window->surface, 10, 62, "persist.txt", 0x00203040U);
    font_draw_text(&window->surface, 10, 84, "readme.txt", 0x00203040U);
    gfx_fill_rect(&window->surface, 10, 110, 320, 2, 0x00C8D4E4U);
    font_draw_text(&window->surface, 10, 120, "zfs mounted, ata primary", 0x004A5A6AU);
}

static void paint_terminal_window(struct wm_window *window) {
    if (window == NULL) {
        return;
    }
    fill_window_body(window);
    gfx_fill_rect(&window->surface, 8, 30, 364, 176, 0x00101820U);
    font_draw_text(&window->surface, 14, 36, "z-os:/# gui", 0x00B9E6A0U);
    font_draw_text(&window->surface, 14, 54, "desktop shell", 0x00B9E6A0U);
    font_draw_text(&window->surface, 14, 72, "ata + zfs online", 0x00B9E6A0U);
    font_draw_text(&window->surface, 14, 90, "mouse events routed", 0x00B9E6A0U);
    font_draw_text(&window->surface, 14, 108, "type exit to leave", 0x00B9E6A0U);
}

static void paint_monitor_window(struct wm_window *window) {
    if (window == NULL) {
        return;
    }
    fill_window_body(window);
    gfx_fill_rect(&window->surface, 12, 34, 316, 16, 0x00D9453DU);
    gfx_fill_rect(&window->surface, 12, 54, 220, 16, 0x002E9E5BU);
    gfx_fill_rect(&window->surface, 12, 74, 260, 16, 0x002F6FB5U);
    font_draw_text(&window->surface, 12, 100, "cpu idle", 0x00203040U);
    font_draw_text(&window->surface, 12, 120, "memory 8 MiB heap", 0x00203040U);
    font_draw_text(&window->surface, 12, 140, "framebuffer 800x600", 0x00203040U);
}

static void paint_app(u32 index, struct wm_window *window) {
    if (index == 0) {
        paint_files_window(window);
    } else if (index == 1) {
        paint_terminal_window(window);
    } else if (index == 2) {
        paint_monitor_window(window);
    } else {
        fill_window_body(window);
        font_draw_text(&window->surface, 16, 40, "click Exit to leave", 0x00203040U);
    }
}

int desktop_open_app(u32 index) {
    if (index >= DESKTOP_MAX_APPS) {
        return -1;
    }
    if (index == 3) {
        exit_requested = true;
        set_action("exit requested");
        return 0;
    }
    if (apps[index].open) {
        wm_raise(apps[index].id);
        wm_set_visible(apps[index].id, true);
        desktop_dirty_mark_full();
        return 0;
    }
    i32 x = 150 + (i32)index * 40;
    i32 y = 60 + (i32)index * 30;
    struct wm_window *window = wm_create(x, y, apps[index].width, apps[index].height, DESKTOP_COLOR_WINDOW_BODY);
    if (window == NULL) {
        return -1;
    }
    wm_set_title(window, apps[index].title);
    paint_app(index, window);
    apps[index].id = window->id;
    apps[index].open = true;
    apps[index].visible = true;
    desktop_dirty_mark_full();
    char action[48];
    ksnprintf(action, sizeof(action), "opened %s", apps[index].name);
    set_action(action);
    return 0;
}

int desktop_close_app(u32 index) {
    if (index >= DESKTOP_MAX_APPS || !apps[index].open) {
        return -1;
    }
    wm_destroy(apps[index].id);
    apps[index].open = false;
    apps[index].visible = false;
    apps[index].id = 0;
    desktop_dirty_mark_full();
    set_action("window closed");
    return 0;
}

void desktop_open_window_for_test(u32 index) {
    desktop_open_app(index);
}

void desktop_render(void) {
    if (desktop_target == NULL || !desktop_target->valid) {
        return;
    }
    gfx_clear(desktop_target, DESKTOP_COLOR_WALLPAPER);
    for (u32 index = 0; index < DESKTOP_ICON_COUNT; index++) {
        draw_icon(index);
    }
    wm_compose(desktop_target);
    draw_status_bar();
    draw_taskbar();
    render_count++;
}

bool desktop_handle_mouse(const struct mouse_event *event) {
    if (event == NULL) {
        return false;
    }
    if (event->type == MOUSE_EVENT_MOVE) {
        if (dragging_id != 0) {
            struct wm_window *window = wm_find((u32)dragging_id);
            if (window != NULL) {
                i32 old_x = window->x;
                i32 old_y = window->y;
                u32 width = window->width;
                u32 height = window->height;
                wm_move_to((u32)dragging_id, event->x - drag_offset_x,
                           event->y - drag_offset_y);
                if (window->x != old_x || window->y != old_y) {
                    dirty_union(old_x, old_y, width, height);
                    dirty_union(window->x, window->y, width, height);
                    set_action("window dragged");
                    return true;
                }
            }
        }
        return false;
    }
    if (event->type != MOUSE_EVENT_BUTTON) {
        return false;
    }
    bool pressed = (event->buttons & MOUSE_BUTTON_LEFT) != 0;
    if (!pressed) {
        if (dragging_id != 0) {
            dragging_id = 0;
            set_action("window moved");
        }
        return false;
    }
    click_count++;
    i32 x = event->x;
    i32 y = event->y;
    if (desktop_taskbar_exit_hit(x, y)) {
        exit_requested = true;
        set_action("exit requested");
        return true;
    }
    u32 app_index = 0;
    if (desktop_taskbar_button_hit(x, y, &app_index) && apps[app_index].open) {
        if (apps[app_index].visible) {
            wm_set_visible(apps[app_index].id, false);
            apps[app_index].visible = false;
            desktop_dirty_mark_full();
            set_action("window hidden");
        } else {
            wm_set_visible(apps[app_index].id, true);
            apps[app_index].visible = true;
            wm_raise(apps[app_index].id);
            desktop_dirty_mark_full();
            set_action("window shown");
        }
        return true;
    }
    if (desktop_icon_at(x, y, &app_index)) {
        desktop_open_app(app_index);
        return true;
    }
    if (y < DESKTOP_STATUS_HEIGHT || y >= 600 - DESKTOP_TASKBAR_HEIGHT) {
        return false;
    }
    struct wm_window *window = wm_window_at_point(x, y);
    if (window == NULL) {
        return false;
    }
    u32 id = window->id;
    i32 window_index = -1;
    for (u32 local = 0; local < DESKTOP_MAX_APPS; local++) {
        if (apps[local].open && apps[local].id == id) {
            window_index = (i32)local;
            break;
        }
    }
    if (wm_close_hit(window, x, y)) {
        if (window_index >= 0) {
            desktop_close_app((u32)window_index);
        } else {
            wm_destroy(id);
        }
        return true;
    }
    wm_raise(id);
    if (wm_title_hit(window, x, y)) {
        dragging_id = (i32)id;
        drag_offset_x = x - window->x;
        drag_offset_y = y - window->y;
        desktop_dirty_mark_full();
        set_action("window drag started");
        return true;
    }
    desktop_dirty_mark_full();
    set_action("window focused");
    return true;
}

bool desktop_handle_key(char key) {
    if (key == 27) {
        exit_requested = true;
        set_action("exit requested");
        return true;
    }
    if (key == '\n' || key == '\r') {
        key_buffer[key_length] = 0;
        if (strcmp(key_buffer, "exit") == 0 || strcmp(key_buffer, "quit") == 0) {
            exit_requested = true;
            set_action("exit requested");
        } else if (strcmp(key_buffer, "close") == 0) {
            for (u32 index = 0; index < DESKTOP_MAX_APPS; index++) {
                if (apps[index].open) {
                    desktop_close_app(index);
                    break;
                }
            }
        } else if (strcmp(key_buffer, "gui") == 0) {
            set_action("gui already active");
        } else if (key_length > 0) {
            set_action("command entered");
        }
        key_length = 0;
        key_buffer[0] = 0;
        return true;
    }
    if (key >= '1' && key <= '4' && key_length == 0) {
        desktop_open_app((u32)(key - '1'));
        return true;
    }
    if (key >= 32 && key <= 126 && key_length + 1 < sizeof(key_buffer)) {
        key_buffer[key_length++] = key;
        return true;
    }
    return false;
}

void desktop_request_exit(void) {
    exit_requested = true;
}

bool desktop_should_exit(void) {
    return exit_requested;
}
