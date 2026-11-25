#ifndef Z_OS_GFX_H
#define Z_OS_GFX_H

#include "types.h"

#define GFX_FORMAT_RGB32 1
#define GFX_FORMAT_BGR32 2

struct gfx_surface {
    u32 *pixels;
    u32 width;
    u32 height;
    u32 pitch_pixels;
    u32 format;
    bool owns_pixels;
    bool valid;
};

void gfx_init_surface(struct gfx_surface *surface, void *pixels, u32 width, u32 height,
                      u32 pitch_pixels, u32 format);
void gfx_clear(struct gfx_surface *surface, u32 color);
void gfx_plot(struct gfx_surface *surface, i32 x, i32 y, u32 color);
u32 gfx_get_pixel(const struct gfx_surface *surface, i32 x, i32 y);
bool gfx_clip(const struct gfx_surface *surface, i32 *x, i32 *y, u32 *width, u32 *height);
void gfx_fill_rect(struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color);
void gfx_hline(struct gfx_surface *surface, i32 x, i32 y, u32 length, u32 color);
void gfx_vline(struct gfx_surface *surface, i32 x, i32 y, u32 length, u32 color);
void gfx_frame_rect(struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color);
void gfx_line(struct gfx_surface *surface, i32 x0, i32 y0, i32 x1, i32 y1, u32 color);
void gfx_copy_rect(struct gfx_surface *destination, const struct gfx_surface *source,
                   i32 source_x, i32 source_y, i32 destination_x, i32 destination_y,
                   u32 width, u32 height);
u32 gfx_checksum(const struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height);
u32 gfx_count_color(const struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color);

#endif
