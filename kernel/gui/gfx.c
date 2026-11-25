#include "gfx.h"
#include "string.h"

void gfx_init_surface(struct gfx_surface *surface, void *pixels, u32 width, u32 height,
                      u32 pitch_pixels, u32 format) {
    if (surface == NULL) {
        return;
    }
    surface->pixels = (u32 *)pixels;
    surface->width = width;
    surface->height = height;
    surface->pitch_pixels = pitch_pixels == 0 ? width : pitch_pixels;
    surface->format = format;
    surface->owns_pixels = false;
    surface->valid = pixels != NULL && width > 0 && height > 0;
}

bool gfx_clip(const struct gfx_surface *surface, i32 *x, i32 *y, u32 *width, u32 *height) {
    if (surface == NULL || !surface->valid || x == NULL || y == NULL || width == NULL || height == NULL) {
        return false;
    }
    if (*width == 0 || *height == 0) {
        return false;
    }
    i32 left = *x;
    i32 top = *y;
    i32 right = *x + (i32)(*width);
    i32 bottom = *y + (i32)(*height);
    if (right <= 0 || bottom <= 0) {
        return false;
    }
    if (left >= (i32)surface->width || top >= (i32)surface->height) {
        return false;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (i32)surface->width) {
        right = (i32)surface->width;
    }
    if (bottom > (i32)surface->height) {
        bottom = (i32)surface->height;
    }
    if (right <= left || bottom <= top) {
        return false;
    }
    *x = left;
    *y = top;
    *width = (u32)(right - left);
    *height = (u32)(bottom - top);
    return true;
}

void gfx_plot(struct gfx_surface *surface, i32 x, i32 y, u32 color) {
    if (surface == NULL || !surface->valid) {
        return;
    }
    if (x < 0 || y < 0 || x >= (i32)surface->width || y >= (i32)surface->height) {
        return;
    }
    surface->pixels[(u32)y * surface->pitch_pixels + (u32)x] = color;
}

u32 gfx_get_pixel(const struct gfx_surface *surface, i32 x, i32 y) {
    if (surface == NULL || !surface->valid) {
        return 0;
    }
    if (x < 0 || y < 0 || x >= (i32)surface->width || y >= (i32)surface->height) {
        return 0;
    }
    return surface->pixels[(u32)y * surface->pitch_pixels + (u32)x];
}

void gfx_clear(struct gfx_surface *surface, u32 color) {
    if (surface == NULL || !surface->valid) {
        return;
    }
    gfx_fill_rect(surface, 0, 0, surface->width, surface->height, color);
}

void gfx_fill_rect(struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color) {
    if (surface == NULL || !surface->valid) {
        return;
    }
    i32 clipped_x = x;
    i32 clipped_y = y;
    u32 clipped_width = width;
    u32 clipped_height = height;
    if (!gfx_clip(surface, &clipped_x, &clipped_y, &clipped_width, &clipped_height)) {
        return;
    }
    for (u32 row = 0; row < clipped_height; row++) {
        u32 *line = surface->pixels + (u32)(clipped_y + (i32)row) * surface->pitch_pixels + (u32)clipped_x;
        for (u32 column = 0; column < clipped_width; column++) {
            line[column] = color;
        }
    }
}

void gfx_hline(struct gfx_surface *surface, i32 x, i32 y, u32 length, u32 color) {
    if (length > 0x7FFFFFFFU) {
        return;
    }
    gfx_fill_rect(surface, x, y, length, 1, color);
}

void gfx_vline(struct gfx_surface *surface, i32 x, i32 y, u32 length, u32 color) {
    if (length > 0x7FFFFFFFU) {
        return;
    }
    gfx_fill_rect(surface, x, y, 1, length, color);
}

void gfx_frame_rect(struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color) {
    if (width == 0 || height == 0) {
        return;
    }
    gfx_hline(surface, x, y, width, color);
    if (height > 1) {
        gfx_hline(surface, x, y + (i32)height - 1, width, color);
    }
    gfx_vline(surface, x, y, height, color);
    if (width > 1) {
        gfx_vline(surface, x + (i32)width - 1, y, height, color);
    }
}

void gfx_copy_rect(struct gfx_surface *destination, const struct gfx_surface *source,
                   i32 source_x, i32 source_y, i32 destination_x, i32 destination_y,
                   u32 width, u32 height) {
    if (destination == NULL || source == NULL || !destination->valid || !source->valid) {
        return;
    }
    for (u32 row = 0; row < height; row++) {
        i32 source_row = source_y + (i32)row;
        i32 destination_row = destination_y + (i32)row;
        if (source_row < 0 || source_row >= (i32)source->height) {
            continue;
        }
        if (destination_row < 0 || destination_row >= (i32)destination->height) {
            continue;
        }
        for (u32 column = 0; column < width; column++) {
            i32 source_column = source_x + (i32)column;
            i32 destination_column = destination_x + (i32)column;
            if (source_column < 0 || source_column >= (i32)source->width) {
                continue;
            }
            if (destination_column < 0 || destination_column >= (i32)destination->width) {
                continue;
            }
            destination->pixels[(u32)destination_row * destination->pitch_pixels + (u32)destination_column] =
                source->pixels[(u32)source_row * source->pitch_pixels + (u32)source_column];
        }
    }
}

u32 gfx_checksum(const struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height) {
    if (surface == NULL || !surface->valid) {
        return 0;
    }
    u32 checksum = 2166136261U;
    for (u32 row = 0; row < height; row++) {
        i32 pixel_y = y + (i32)row;
        if (pixel_y < 0 || pixel_y >= (i32)surface->height) {
            continue;
        }
        for (u32 column = 0; column < width; column++) {
            i32 pixel_x = x + (i32)column;
            if (pixel_x < 0 || pixel_x >= (i32)surface->width) {
                continue;
            }
            u32 value = surface->pixels[(u32)pixel_y * surface->pitch_pixels + (u32)pixel_x];
            checksum ^= value;
            checksum *= 16777619U;
        }
    }
    return checksum;
}

u32 gfx_count_color(const struct gfx_surface *surface, i32 x, i32 y, u32 width, u32 height, u32 color) {
    if (surface == NULL || !surface->valid) {
        return 0;
    }
    u32 count = 0;
    for (u32 row = 0; row < height; row++) {
        i32 pixel_y = y + (i32)row;
        if (pixel_y < 0 || pixel_y >= (i32)surface->height) {
            continue;
        }
        for (u32 column = 0; column < width; column++) {
            i32 pixel_x = x + (i32)column;
            if (pixel_x < 0 || pixel_x >= (i32)surface->width) {
                continue;
            }
            if (surface->pixels[(u32)pixel_y * surface->pitch_pixels + (u32)pixel_x] == color) {
                count++;
            }
        }
    }
    return count;
}

void gfx_line(struct gfx_surface *surface, i32 x0, i32 y0, i32 x1, i32 y1, u32 color) {
    if (surface == NULL || !surface->valid) {
        return;
    }
    i32 delta_x = x1 > x0 ? x1 - x0 : x0 - x1;
    i32 delta_y = y1 > y0 ? y1 - y0 : y0 - y1;
    i32 step_x = x0 < x1 ? 1 : -1;
    i32 step_y = y0 < y1 ? 1 : -1;
    i32 error = delta_x - delta_y;
    i32 x = x0;
    i32 y = y0;
    for (;;) {
        gfx_plot(surface, x, y, color);
        if (x == x1 && y == y1) {
            break;
        }
        i32 doubled = error * 2;
        if (doubled > -delta_y) {
            error -= delta_y;
            x += step_x;
        }
        if (doubled < delta_x) {
            error += delta_x;
            y += step_y;
        }
    }
}
