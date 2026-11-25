#include "userlib.h"

#define COLOR_BACKGROUND 0x00102030U
#define COLOR_RED 0x00FF0000U
#define COLOR_GREEN 0x0000FF00U
#define COLOR_BLUE 0x000000FFU

static const char gui_start[] = "guitest: graphics program started\n";
static const char gui_drawn[] = "guitest: rectangles drawn to framebuffer\n";
static const char gui_pixel[] = "guitest: pixel readback verified\n";
static const char gui_info[] = "guitest: framebuffer info received\n";
static const char gui_fail[] = "guitest: graphics validation failed\n";

__attribute__((noreturn)) void _start(void) {
    static struct fb_user_info info;
    zwrite(gui_start);
    if (zfb_info(&info) != 0 || info.active == 0 || info.width < 480 || info.height < 360) {
        zwrite(gui_fail);
        zexit(3);
    }
    zwrite(gui_info);
    if (zfb_rect(0, 0, info.width, info.height, COLOR_BACKGROUND) != 0) {
        zwrite(gui_fail);
        zexit(4);
    }
    if (zfb_rect(32, 32, 128, 96, COLOR_RED) != 0 ||
        zfb_rect(200, 64, 96, 96, COLOR_GREEN) != 0 ||
        zfb_rect(360, 96, 64, 64, COLOR_BLUE) != 0) {
        zwrite(gui_fail);
        zexit(5);
    }
    zwrite(gui_drawn);
    if (zfb_pixel(36, 36) != (long)COLOR_RED ||
        zfb_pixel(204, 68) != (long)COLOR_GREEN ||
        zfb_pixel(364, 100) != (long)COLOR_BLUE ||
        zfb_pixel(0, 0) != (long)COLOR_BACKGROUND) {
        zwrite(gui_fail);
        zexit(6);
    }
    zwrite(gui_pixel);
    zexit(0);
}
