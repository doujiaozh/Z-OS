#include "fb.h"
#include "font.h"
#include "heap.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define VBE_DISPI_INDEX_ID 0
#define VBE_DISPI_INDEX_XRES 1
#define VBE_DISPI_INDEX_YRES 2
#define VBE_DISPI_INDEX_BPP 3
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_INDEX_VIRT_WIDTH 6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ID_MASK 0xFFF0
#define VBE_DISPI_ID_BASE 0xB0C0

struct fb_state {
    bool device_found;
    bool dispi;
    bool mode_active;
    u64 pci_address;
    u64 physical;
    u64 mapped_size;
    u32 mapped_pages;
    struct fb_mode mode;
    struct gfx_surface surface;
    u32 *shadow_pixels;
    u32 shadow_pixels_count;
};

static struct fb_state state;

static u32 pci_read_config32(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 address = 0x80000000U | ((u32)bus << 16) | ((u32)slot << 11) |
                  ((u32)function << 8) | ((u32)offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static u16 dispi_read(u16 index) {
    outw(0x1CE, index);
    return inw(0x1CF);
}

static void dispi_write(u16 index, u16 value) {
    outw(0x1CE, index);
    outw(0x1CF, value);
}

static u64 find_vga_bar0(void) {
    for (u32 slot = 0; slot < 32; slot++) {
        u32 identifier = pci_read_config32(0, (u8)slot, 0, 0);
        if ((identifier & 0xFFFF) == 0xFFFF) {
            continue;
        }
        u32 class_register = pci_read_config32(0, (u8)slot, 0, 0x08);
        u32 class_code = (class_register >> 24) & 0xFF;
        if (class_code != 0x03) {
            continue;
        }
        u32 bar0 = pci_read_config32(0, (u8)slot, 0, 0x10);
        if ((bar0 & 0x1) != 0) {
            continue;
        }
        u64 address = (u64)(bar0 & 0xFFFFFFF0U);
        if (address == 0) {
            continue;
        }
        return address;
    }
    return 0;
}

static int map_vram(u64 physical, u64 size) {
    if (size > FB_MAP_MAX_SIZE) {
        size = FB_MAP_MAX_SIZE;
    }
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 base = vmm_device_base();
    for (u64 index = 0; index < pages; index++) {
        u64 virtual_address = base + index * PAGE_SIZE;
        u64 frame = physical + index * PAGE_SIZE;
        if (vmm_map_device_page(virtual_address, frame,
                                PTE_PRESENT | PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_WRITE_THROUGH) != 0) {
            return -1;
        }
    }
    state.mapped_size = pages * PAGE_SIZE;
    state.mapped_pages = (u32)pages;
    return 0;
}

int fb_init(void) {
    memset(&state, 0, sizeof(state));
    state.pci_address = find_vga_bar0();
    if (state.pci_address == 0) {
        return -1;
    }
    state.device_found = true;
    state.physical = state.pci_address;
    state.dispi = (dispi_read(VBE_DISPI_INDEX_ID) & VBE_DISPI_ID_MASK) == VBE_DISPI_ID_BASE;
    if (state.dispi) {
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    }
    return 0;
}

bool fb_device_found(void) {
    return state.device_found;
}

bool fb_dispi_supported(void) {
    return state.dispi;
}

bool fb_mode_active(void) {
    return state.mode_active;
}

u64 fb_pci_address(void) {
    return state.pci_address;
}

struct fb_mode fb_current_mode(void) {
    return state.mode;
}

struct gfx_surface *fb_surface(void) {
    return state.mode_active ? &state.surface : NULL;
}

static int fb_begin_surface(u32 width, u32 height, u32 pitch, bool shadow) {
    state.mode_active = true;
    state.mode.width = width;
    state.mode.height = height;
    state.mode.pitch = pitch;
    state.mode.bits_per_pixel = 32;
    state.mode.physical = state.physical;
    state.mode.format = GFX_FORMAT_RGB32;
    state.mode.shadow = shadow;
    if (shadow) {
        u64 bytes = (u64)pitch * height;
        u32 count = (u32)((bytes + 3) / 4);
        if (state.shadow_pixels != NULL && state.shadow_pixels_count >= count) {
            gfx_init_surface(&state.surface, state.shadow_pixels, width, height, pitch / 4, GFX_FORMAT_RGB32);
            gfx_clear(&state.surface, 0);
            return 0;
        }
        if (state.shadow_pixels != NULL) {
            kfree(state.shadow_pixels);
            state.shadow_pixels = NULL;
        }
        state.shadow_pixels = kzalloc((usize)bytes);
        if (state.shadow_pixels == NULL) {
            state.mode_active = false;
            return -1;
        }
        state.shadow_pixels_count = count;
        gfx_init_surface(&state.surface, state.shadow_pixels, width, height, pitch / 4, GFX_FORMAT_RGB32);
        gfx_clear(&state.surface, 0);
        return 0;
    }
    if (map_vram(state.physical, (u64)pitch * height) != 0) {
        state.mode_active = false;
        return -1;
    }
    gfx_init_surface(&state.surface, (void *)(usize)vmm_device_base(), width, height, pitch / 4, GFX_FORMAT_RGB32);
    return 0;
}

int fb_set_mode(u32 width, u32 height, u32 bits_per_pixel) {
    if (width == 0 || height == 0 || bits_per_pixel != 32) {
        return -1;
    }
    if (!state.device_found) {
        return -2;
    }
    if (state.dispi) {
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        dispi_write(VBE_DISPI_INDEX_XRES, (u16)width);
        dispi_write(VBE_DISPI_INDEX_YRES, (u16)height);
        dispi_write(VBE_DISPI_INDEX_BPP, (u16)bits_per_pixel);
        dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, (u16)width);
        dispi_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (u16)height);
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        u16 applied_width = dispi_read(VBE_DISPI_INDEX_XRES);
        u16 applied_height = dispi_read(VBE_DISPI_INDEX_YRES);
        if (applied_width != (u16)width || applied_height != (u16)height) {
            dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
            return -3;
        }
        if (fb_begin_surface(width, height, width * 4, false) != 0) {
            dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
            return -4;
        }
        return 0;
    }
    if (fb_begin_surface(width, height, width * 4, true) != 0) {
        return -4;
    }
    return 0;
}

void fb_restore_text(void) {
    if (!state.mode_active) {
        return;
    }
    if (!state.mode.shadow && state.dispi) {
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    }
    state.mode_active = false;
    memset(&state.surface, 0, sizeof(state.surface));
}

u32 fb_pixel(u32 x, u32 y) {
    if (!state.mode_active || x >= state.mode.width || y >= state.mode.height) {
        return 0;
    }
    return gfx_get_pixel(&state.surface, (i32)x, (i32)y);
}

int fb_fill(u32 x, u32 y, u32 width, u32 height, u32 color) {
    if (!state.mode_active) {
        return -1;
    }
    gfx_fill_rect(&state.surface, (i32)x, (i32)y, width, height, color);
    return 0;
}

u64 fb_vram_physical_offset(u32 x, u32 y) {
    if (!state.mode_active) {
        return 0;
    }
    return state.mode.physical + (u64)y * state.mode.pitch + (u64)x * 4;
}

u32 fb_shadow_pixel(u32 x, u32 y) {
    return fb_pixel(x, y);
}

int fb_draw_text(u32 x, u32 y, const char *text, u32 color) {
    if (!state.mode_active || text == NULL) {
        return -1;
    }
    return font_draw_text(&state.surface, (i32)x, (i32)y, text, color);
}

int fb_write_test_pixels(u64 byte_offset, const u32 *values, u32 count) {
    if (!state.device_found || state.mode_active || values == NULL || count == 0) {
        return -1;
    }
    if (byte_offset + (u64)count * 4 > FB_MAP_MAX_SIZE) {
        return -1;
    }
    if (state.mapped_size == 0) {
        if (map_vram(state.physical, FB_MAP_MAX_SIZE) != 0) {
            return -1;
        }
    }
    volatile u32 *window = (volatile u32 *)(usize)(vmm_device_base() + byte_offset);
    for (u32 index = 0; index < count; index++) {
        window[index] = values[index];
    }
    return 0;
}

int fb_read_test_pixels(u64 byte_offset, u32 *values, u32 count) {
    if (!state.device_found || state.mode_active || values == NULL || count == 0) {
        return -1;
    }
    if (byte_offset + (u64)count * 4 > FB_MAP_MAX_SIZE) {
        return -1;
    }
    if (state.mapped_size == 0) {
        if (map_vram(state.physical, FB_MAP_MAX_SIZE) != 0) {
            return -1;
        }
    }
    volatile u32 *window = (volatile u32 *)(usize)(vmm_device_base() + byte_offset);
    for (u32 index = 0; index < count; index++) {
        values[index] = window[index];
    }
    return 0;
}
