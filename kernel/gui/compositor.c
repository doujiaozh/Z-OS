#include "compositor.h"
#include "heap.h"
#include "string.h"

static u32 next_layer_id = 1;

int compositor_init(struct compositor *compositor, struct gfx_surface *target,
                    void (*flush)(struct compositor *compositor)) {
    if (compositor == NULL || target == NULL || !target->valid) {
        return -1;
    }
    memset(compositor, 0, sizeof(struct compositor));
    compositor->used = true;
    compositor->target = target;
    compositor->flush = flush;
    next_layer_id = 1;
    return 0;
}

struct compositor_layer *compositor_find_layer(struct compositor *compositor, u32 id) {
    if (compositor == NULL || !compositor->used) {
        return NULL;
    }
    for (u32 index = 0; index < COMPOSITOR_MAX_LAYERS; index++) {
        if (compositor->layers[index].used && compositor->layers[index].id == id) {
            return &compositor->layers[index];
        }
    }
    return NULL;
}

struct compositor_layer *compositor_add_layer(struct compositor *compositor, i32 x, i32 y,
                                              u32 width, u32 height, u32 background) {
    if (compositor == NULL || !compositor->used || width == 0 || height == 0) {
        return NULL;
    }
    u32 slot = COMPOSITOR_MAX_LAYERS;
    for (u32 index = 0; index < COMPOSITOR_MAX_LAYERS; index++) {
        if (!compositor->layers[index].used) {
            slot = index;
            break;
        }
    }
    if (slot == COMPOSITOR_MAX_LAYERS) {
        return NULL;
    }
    u32 *pixels = kzalloc((usize)width * height * 4);
    if (pixels == NULL) {
        return NULL;
    }
    struct compositor_layer *layer = &compositor->layers[slot];
    memset(layer, 0, sizeof(struct compositor_layer));
    layer->used = true;
    layer->id = next_layer_id++;
    layer->x = x;
    layer->y = y;
    layer->visible = true;
    gfx_init_surface(&layer->surface, pixels, width, height, width, GFX_FORMAT_RGB32);
    gfx_clear(&layer->surface, background);
    compositor->layer_count++;
    return layer;
}

int compositor_remove_layer(struct compositor *compositor, u32 id) {
    struct compositor_layer *layer = compositor_find_layer(compositor, id);
    if (layer == NULL) {
        return -1;
    }
    if (layer->surface.pixels != NULL) {
        kfree(layer->surface.pixels);
    }
    memset(layer, 0, sizeof(struct compositor_layer));
    if (compositor->layer_count > 0) {
        compositor->layer_count--;
    }
    return 0;
}

int compositor_move_layer(struct compositor *compositor, u32 id, i32 x, i32 y) {
    struct compositor_layer *layer = compositor_find_layer(compositor, id);
    if (layer == NULL) {
        return -1;
    }
    layer->x = x;
    layer->y = y;
    return 0;
}

u32 compositor_layer_count(struct compositor *compositor) {
    return compositor != NULL ? compositor->layer_count : 0;
}

void compositor_damage(struct compositor *compositor, i32 x, i32 y, u32 width, u32 height) {
    if (compositor == NULL || !compositor->used || width == 0 || height == 0) {
        return;
    }
    if (compositor->damage_count < COMPOSITOR_MAX_DAMAGE) {
        struct gfx_rect *rect = &compositor->damage[compositor->damage_count];
        rect->x = x;
        rect->y = y;
        rect->width = width;
        rect->height = height;
        compositor->damage_count++;
        return;
    }
    struct gfx_rect *last = &compositor->damage[COMPOSITOR_MAX_DAMAGE - 1];
    i32 left = x < last->x ? x : last->x;
    i32 top = y < last->y ? y : last->y;
    i32 right_a = x + (i32)width;
    i32 right_b = last->x + (i32)last->width;
    i32 bottom_a = y + (i32)height;
    i32 bottom_b = last->y + (i32)last->height;
    i32 right = right_a > right_b ? right_a : right_b;
    i32 bottom = bottom_a > bottom_b ? bottom_a : bottom_b;
    last->x = left;
    last->y = top;
    last->width = (u32)(right - left);
    last->height = (u32)(bottom - top);
}

void compositor_clear_damage(struct compositor *compositor) {
    if (compositor == NULL) {
        return;
    }
    memset(compositor->damage, 0, sizeof(compositor->damage));
    compositor->damage_count = 0;
}

u32 compositor_damage_count(struct compositor *compositor) {
    return compositor != NULL ? compositor->damage_count : 0;
}

int compositor_composite(struct compositor *compositor) {
    if (compositor == NULL || !compositor->used || compositor->target == NULL) {
        return -1;
    }
    if (compositor->target->valid) {
        gfx_clear(compositor->target, 0x00101820U);
    }
    for (u32 index = 0; index < COMPOSITOR_MAX_LAYERS; index++) {
        struct compositor_layer *layer = &compositor->layers[index];
        if (!layer->used || !layer->visible) {
            continue;
        }
        gfx_copy_rect(compositor->target, &layer->surface, 0, 0, layer->x, layer->y,
                      layer->surface.width, layer->surface.height);
    }
    compositor->composites++;
    return 0;
}

int compositor_present(struct compositor *compositor) {
    if (compositor == NULL || !compositor->used) {
        return -1;
    }
    if (compositor_composite(compositor) != 0) {
        return -1;
    }
    compositor_clear_damage(compositor);
    if (compositor->flush != NULL) {
        compositor->flush(compositor);
        compositor->flush_count++;
    }
    return 0;
}
