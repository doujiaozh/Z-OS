#include "wm.h"
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
