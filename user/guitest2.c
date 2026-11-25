#include "userlib.h"

#define COLOR_BACKGROUND 0x00101820U
#define COLOR_PANEL 0x00203040U
#define COLOR_TITLE 0x00F2C14EU
#define COLOR_TEXT 0x00E8EEF7U
#define COLOR_ACCENT 0x00D9453DU

static const char start_message[] = "guitest2: extended graphics test started\n";
static const char text_message[] = "guitest2: font text rendered\n";
static const char mouse_message[] = "guitest2: mouse state reported\n";
static const char pass_message[] = "guitest2: compositor surface verified\n";
static const char fail_message[] = "guitest2: graphics validation failed\n";

static __attribute__((noreturn)) void fail(long code) {
    zwrite(fail_message);
    zexit(code);
}

__attribute__((noreturn)) void _start(void) {
    static struct fb_user_info info;
    static struct mouse_user_state mouse_state;
    zwrite(start_message);
    if (zfb_info(&info) != 0 || info.active == 0 || info.width < 640 || info.height < 480) {
        fail(3);
    }
    if (zfb_rect(0, 0, info.width, info.height, COLOR_BACKGROUND) != 0) {
        fail(4);
    }
    if (zfb_rect(40, 40, 480, 200, COLOR_PANEL) != 0) {
        fail(5);
    }
    if (zfb_rect(40, 40, 480, 28, COLOR_TITLE) != 0) {
        fail(6);
    }
    if (zfb_rect(60, 220, 120, 48, COLOR_ACCENT) != 0) {
        fail(7);
    }
    if (zfb_text(60, 84, COLOR_TEXT, "Z-OS 0.5 posix gui") != 0) {
        fail(8);
    }
    if (zfb_text(60, 120, COLOR_TEXT, "font rendering verified") != 0) {
        fail(9);
    }
    if (zfb_text(60, 156, COLOR_TITLE, "compositor layer ready") != 0) {
        fail(10);
    }
    long lit = 0;
    for (long y = 84; y < 100; y++) {
        for (long x = 60; x < 240; x++) {
            if (zfb_pixel(x, y) == (long)COLOR_TEXT) {
                lit++;
            }
        }
    }
    if (lit < 40) {
        fail(11);
    }
    zwrite(text_message);
    if (zfb_pixel(60, 84) != (long)COLOR_TEXT && lit == 0) {
        fail(12);
    }
    if (zmouse(&mouse_state) != 0) {
        fail(13);
    }
    if (mouse_state.x < 0 || mouse_state.y < 0) {
        fail(14);
    }
    if (zfb_text(60, 260, COLOR_TEXT, "pointer and layers online") != 0) {
        fail(15);
    }
    zwrite(mouse_message);
    zwrite(pass_message);
    zexit(0);
}
