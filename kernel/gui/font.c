#include "font.h"
#include "font_data.h"
#include "string.h"

const u8 *font_glyph(char character) {
    u32 code = (u8)character;
    if (code < FONT_FIRST_CHAR || code > FONT_LAST_CHAR) {
        code = (u32)'?';
    }
    return font8x16[code - FONT_FIRST_CHAR];
}

u32 font_glyph_pixels(char character) {
    const u8 *rows = font_glyph(character);
    u32 count = 0;
    for (u32 row = 0; row < FONT_CHAR_HEIGHT; row++) {
        u8 value = rows[row];
        while (value != 0) {
            count += value & 1U;
            value >>= 1;
        }
    }
    return count;
}

u32 font_glyph_row(char character, u32 row) {
    if (row >= FONT_CHAR_HEIGHT) {
        return 0;
    }
    return font_glyph(character)[row];
}

int font_draw_char(struct gfx_surface *surface, i32 x, i32 y, char character, u32 color,
                   bool opaque, u32 background) {
    if (surface == NULL || !surface->valid) {
        return -1;
    }
    const u8 *rows = font_glyph(character);
    for (u32 row = 0; row < FONT_CHAR_HEIGHT; row++) {
        u8 bits = rows[row];
        for (u32 column = 0; column < FONT_CHAR_WIDTH; column++) {
            bool set = (bits & (0x80U >> column)) != 0;
            if (set) {
                gfx_plot(surface, x + (i32)column, y + (i32)row, color);
            } else if (opaque) {
                gfx_plot(surface, x + (i32)column, y + (i32)row, background);
            }
        }
    }
    return 0;
}

int font_draw_text(struct gfx_surface *surface, i32 x, i32 y, const char *text, u32 color) {
    if (surface == NULL || !surface->valid || text == NULL) {
        return -1;
    }
    i32 cursor = x;
    for (u32 index = 0; text[index] != 0; index++) {
        if (surface->width != 0 && cursor + FONT_CHAR_WIDTH > (i32)surface->width) {
            break;
        }
        font_draw_char(surface, cursor, y, text[index], color, false, 0);
        cursor += FONT_CHAR_WIDTH;
    }
    return 0;
}

u32 font_text_width(const char *text) {
    if (text == NULL) {
        return 0;
    }
    return (u32)strlen(text) * FONT_CHAR_WIDTH;
}
