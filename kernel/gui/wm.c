#include "wm.h"
#include "font.h"
#include "heap.h"
#include "lib.h"
#include "string.h"

static struct wm_window windows[WM_MAX_WINDOWS];
static u32 next_window_id = 1;
static u32 window_count = 0;
static u32 window_order[WM_MAX_WINDOWS];

void wm_init(void) {
    memset(windows, 0, sizeof(windows));
    memset(window_order, 0, sizeof(window_order));
    next_window_id = 1;
    window_count = 0;
    kprintf("WM: window manager ready for %u windows\n", WM_MAX_WINDOWS);
}

struct wm_window *wm_window_at(u32 index) {
    if (index >= WM_MAX_WINDOWS || !windows[index].used) {
        return NULL;
    }
    return &windows[index];
}

struct wm_window *wm_find(u32 id) {
    for (u32 index = 0; index < WM_MAX_WINDOWS; index++) {
        if (windows[index].used && windows[index].id == id) {
            return &windows[index];
        }
    }
    return NULL;
}

u32 wm_count(void) {
    return window_count;
}

u32 wm_zorder_at(u32 index) {
    if (index >= window_count) {
        return 0;
    }
    return window_order[index];
}

struct wm_window *wm_create(i32 x, i32 y, u32 width, u32 height, u32 background) {
    if (width == 0 || height == 0 || width > WM_MAX_WIDTH || height > WM_MAX_HEIGHT) {
        return NULL;
    }
    if (window_count >= WM_MAX_WINDOWS) {
        return NULL;
    }
    u32 slot = WM_MAX_WINDOWS;
    for (u32 index = 0; index < WM_MAX_WINDOWS; index++) {
        if (!windows[index].used) {
            slot = index;
            break;
        }
    }
    if (slot == WM_MAX_WINDOWS) {
        return NULL;
    }
    usize bytes = (usize)width * height * 4;
    u32 *pixels = kzalloc(bytes);
    if (pixels == NULL) {
        return NULL;
    }
    struct wm_window *window = &windows[slot];
    memset(window, 0, sizeof(*window));
    window->used = true;
    window->id = next_window_id++;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->background = background;
    window->visible = true;
    window->owns_surface = true;
    gfx_init_surface(&window->surface, pixels, width, height, width, GFX_FORMAT_RGB32);
    gfx_clear(&window->surface, background);
    window_order[window_count++] = window->id;
    return window;
}

static void remove_from_order(u32 id) {
    u32 position = window_count;
    for (u32 index = 0; index < window_count; index++) {
        if (window_order[index] == id) {
            position = index;
            break;
        }
    }
    if (position >= window_count) {
        return;
    }
    for (u32 index = position; index + 1 < window_count; index++) {
        window_order[index] = window_order[index + 1];
    }
    if (window_count > 0) {
        window_count--;
        window_order[window_count] = 0;
    }
}

int wm_destroy(u32 id) {
    struct wm_window *window = wm_find(id);
    if (window == NULL) {
        return -1;
    }
    if (window->owns_surface && window->surface.pixels != NULL) {
        kfree(window->surface.pixels);
    }
    memset(window, 0, sizeof(*window));
    remove_from_order(id);
    return 0;
}

int wm_fill(u32 id, i32 x, i32 y, u32 width, u32 height, u32 color) {
    struct wm_window *window = wm_find(id);
    if (window == NULL) {
        return -1;
    }
    gfx_fill_rect(&window->surface, x, y, width, height, color);
    return 0;
}

int wm_clear(u32 id, u32 color) {
    struct wm_window *window = wm_find(id);
    if (window == NULL) {
        return -1;
    }
    window->background = color;
    gfx_clear(&window->surface, color);
    return 0;
}

int wm_draw_to(struct gfx_surface *target) {
    if (target == NULL || !target->valid) {
        return -1;
    }
    for (u32 index = 0; index < window_count; index++) {
        struct wm_window *window = wm_find(window_order[index]);
        if (window == NULL || !window->visible) {
            continue;
        }
        gfx_copy_rect(target, &window->surface, 0, 0, window->x, window->y,
                      window->width, window->height);
    }
    return 0;
}

void wm_set_title(struct wm_window *window, const char *title) {
    if (window == NULL) {
        return;
    }
    if (title != NULL) {
        strncpy(window->title, title, sizeof(window->title) - 1);
        window->title[sizeof(window->title) - 1] = 0;
        window->has_title = true;
    } else {
        window->title[0] = 0;
        window->has_title = false;
    }
}

bool wm_point_in_window(const struct wm_window *window, i32 x, i32 y) {
    if (window == NULL || !window->used || !window->visible) {
        return false;
    }
    return x >= window->x && y >= window->y &&
           x < window->x + (i32)window->width && y < window->y + (i32)window->height;
}

bool wm_close_hit(const struct wm_window *window, i32 x, i32 y) {
    if (window == NULL || !window->used || !window->has_title) {
        return false;
    }
    i32 left = window->x + (i32)window->width - WM_CLOSE_SIZE - 4;
    i32 top = window->y + 3;
    return x >= left && x < left + WM_CLOSE_SIZE && y >= top && y < top + WM_CLOSE_SIZE;
}

bool wm_title_hit(const struct wm_window *window, i32 x, i32 y) {
    if (!wm_point_in_window(window, x, y) || !window->has_title) {
        return false;
    }
    if (wm_close_hit(window, x, y)) {
        return false;
    }
    return y >= window->y && y < window->y + WM_TITLE_HEIGHT;
}

struct wm_window *wm_window_at_point(i32 x, i32 y) {
    for (u32 index = window_count; index > 0; index--) {
        struct wm_window *window = wm_find(window_order[index - 1]);
        if (window != NULL && wm_point_in_window(window, x, y)) {
            return window;
        }
    }
    return NULL;
}

void wm_raise(u32 id) {
    u32 position = window_count;
    for (u32 index = 0; index < window_count; index++) {
        if (window_order[index] == id) {
            position = index;
            break;
        }
    }
    if (position >= window_count) {
        return;
    }
    for (u32 index = position; index + 1 < window_count; index++) {
        window_order[index] = window_order[index + 1];
    }
    window_order[window_count - 1] = id;
}

void wm_set_visible(u32 id, bool visible) {
    struct wm_window *window = wm_find(id);
    if (window != NULL) {
        window->visible = visible;
    }
}

void wm_move_to(u32 id, i32 x, i32 y) {
    struct wm_window *window = wm_find(id);
    if (window == NULL) {
        return;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x + (i32)window->width > 800) {
        x = 800 - (i32)window->width;
    }
    if (y + (i32)window->height > 600) {
        y = 600 - (i32)window->height;
    }
    window->x = x;
    window->y = y;
}

void wm_draw_frame(struct gfx_surface *target, const struct wm_window *window) {
    if (target == NULL || window == NULL || !window->used) {
        return;
    }
    gfx_fill_rect(target, window->x + 4, window->y + 4, window->width, window->height, 0x00080C12U);
    if (!window->has_title) {
        gfx_frame_rect(target, window->x, window->y, window->width, window->height, 0x006B7C93U);
        return;
    }
    gfx_fill_rect(target, window->x, window->y, window->width, WM_TITLE_HEIGHT, 0x002F6FB5U);
    gfx_frame_rect(target, window->x, window->y, window->width, window->height, 0x006B7C93U);
    font_draw_text(target, window->x + 6, window->y + 1, window->title, 0x00F4F8FFU);
    i32 left = window->x + (i32)window->width - WM_CLOSE_SIZE - 4;
    i32 top = window->y + 3;
    gfx_fill_rect(target, left, top, WM_CLOSE_SIZE, WM_CLOSE_SIZE, 0x00C43B3BU);
    gfx_line(target, left + 3, top + 3, left + WM_CLOSE_SIZE - 4, top + WM_CLOSE_SIZE - 4, 0x00FFFFFFU);
    gfx_line(target, left + WM_CLOSE_SIZE - 4, top + 3, left + 3, top + WM_CLOSE_SIZE - 4, 0x00FFFFFFU);
}

int wm_compose(struct gfx_surface *target) {
    if (target == NULL || !target->valid) {
        return -1;
    }
    for (u32 index = 0; index < window_count; index++) {
        struct wm_window *window = wm_find(window_order[index]);
        if (window == NULL || !window->visible) {
            continue;
        }
        gfx_copy_rect(target, &window->surface, 0, 0, window->x, window->y,
                      window->width, window->height);
        wm_draw_frame(target, window);
    }
    return 0;
}
