#ifndef Z_OS_FONT_H
#define Z_OS_FONT_H

#include "gfx.h"
#include "types.h"

#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16

const u8 *font_glyph(char character);
u32 font_glyph_pixels(char character);
u32 font_glyph_row(char character, u32 row);
int font_draw_char(struct gfx_surface *surface, i32 x, i32 y, char character, u32 color,
                   bool opaque, u32 background);
int font_draw_text(struct gfx_surface *surface, i32 x, i32 y, const char *text, u32 color);
u32 font_text_width(const char *text);

#endif
