#ifndef Z_OS_COMPOSITOR_H
#define Z_OS_COMPOSITOR_H

#include "gfx.h"
#include "types.h"

#define COMPOSITOR_MAX_LAYERS 8
#define COMPOSITOR_MAX_DAMAGE 8

struct gfx_rect {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
};

struct compositor_layer {
    bool used;
    u32 id;
    i32 x;
    i32 y;
    bool visible;
    struct gfx_surface surface;
};

struct compositor;

struct compositor {
    bool used;
    struct gfx_surface *target;
    void (*flush)(struct compositor *compositor);
    struct compositor_layer layers[COMPOSITOR_MAX_LAYERS];
    u32 layer_count;
    u32 composites;
    u32 flush_count;
    struct gfx_rect damage[COMPOSITOR_MAX_DAMAGE];
    u32 damage_count;
};

int compositor_init(struct compositor *compositor, struct gfx_surface *target,
                    void (*flush)(struct compositor *compositor));
struct compositor_layer *compositor_add_layer(struct compositor *compositor, i32 x, i32 y,
                                              u32 width, u32 height, u32 background);
struct compositor_layer *compositor_find_layer(struct compositor *compositor, u32 id);
int compositor_remove_layer(struct compositor *compositor, u32 id);
int compositor_move_layer(struct compositor *compositor, u32 id, i32 x, i32 y);
u32 compositor_layer_count(struct compositor *compositor);
void compositor_damage(struct compositor *compositor, i32 x, i32 y, u32 width, u32 height);
void compositor_clear_damage(struct compositor *compositor);
u32 compositor_damage_count(struct compositor *compositor);
int compositor_composite(struct compositor *compositor);
int compositor_present(struct compositor *compositor);

#endif
