#ifndef Z_OS_FB_H
#define Z_OS_FB_H

#include "gfx.h"
#include "types.h"

#define FB_MAP_MAX_SIZE (8ULL * 1024 * 1024)
#define FB_DEFAULT_WIDTH 800
#define FB_DEFAULT_HEIGHT 600
#define FB_DEFAULT_BPP 32

struct fb_mode {
    u32 width;
    u32 height;
    u32 pitch;
    u32 bits_per_pixel;
    u64 physical;
    u32 format;
    bool shadow;
};

int fb_init(void);
bool fb_device_found(void);
bool fb_dispi_supported(void);
bool fb_mode_active(void);
u64 fb_pci_address(void);
struct fb_mode fb_current_mode(void);
int fb_set_mode(u32 width, u32 height, u32 bits_per_pixel);
void fb_restore_text(void);
struct gfx_surface *fb_surface(void);
u32 fb_pixel(u32 x, u32 y);
int fb_fill(u32 x, u32 y, u32 width, u32 height, u32 color);
u64 fb_vram_physical_offset(u32 x, u32 y);
u32 fb_shadow_pixel(u32 x, u32 y);
int fb_write_test_pixels(u64 byte_offset, const u32 *values, u32 count);
int fb_read_test_pixels(u64 byte_offset, u32 *values, u32 count);

#endif
