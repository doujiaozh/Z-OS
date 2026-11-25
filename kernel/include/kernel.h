#ifndef Z_OS_KERNEL_H
#define Z_OS_KERNEL_H

#include "types.h"

#define Z_OS_NAME "Z-OS"
#define Z_OS_VERSION "0.6.0"
#define Z_OS_CODENAME "Desktop"

#define DISPLAY_MAX_DEVICES 8
#define FRAMEBUFFER_FORMAT_UNKNOWN 0
#define FRAMEBUFFER_FORMAT_RGB 1
#define FRAMEBUFFER_FORMAT_BGR 2

struct framebuffer {
    u64 address;
    u64 size;
    u32 width;
    u32 height;
    u32 pitch;
    u8 bits_per_pixel;
    u8 red_size;
    u8 red_position;
    u8 green_size;
    u8 green_position;
    u8 blue_size;
    u8 blue_position;
    u8 reserved;
    u32 format;
    u32 flags;
};

struct display_device;

struct display_operations {
    const char *name;
    int (*initialize)(struct display_device *device);
    int (*set_mode)(struct display_device *device, const struct framebuffer *framebuffer);
    int (*present)(struct display_device *device, u32 x, u32 y, u32 width, u32 height);
    void (*shutdown)(struct display_device *device);
};

struct display_device {
    char name[32];
    struct framebuffer framebuffer;
    const struct display_operations *operations;
    void *driver_data;
    bool registered;
};

int display_register(struct display_device *device);
int display_unregister(struct display_device *device);
struct display_device *display_find(const char *name);
struct display_device *display_primary(void);
u32 display_count(void);

struct boot_info;

void kmain(struct boot_info *boot);

#endif
