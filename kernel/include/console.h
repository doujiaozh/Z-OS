#ifndef Z_OS_CONSOLE_H
#define Z_OS_CONSOLE_H

#include "types.h"

#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GREY 7
#define COLOR_DARK_GREY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

void console_init(void);
void console_clear(void);
void console_putc(char character);
void console_write(const char *text);
void console_write_n(const char *text, usize count);
void console_write_line(const char *text);
void console_set_color(u8 foreground, u8 background);
void console_reset_color(void);
void console_backspace(void);
u32 console_columns(void);
u32 console_rows(void);

#endif
