#include "kernel.h"
#include "spinlock.h"
#include "string.h"

static struct display_device *display_devices[DISPLAY_MAX_DEVICES];
static spinlock_t display_lock = SPINLOCK_INIT;

int display_register(struct display_device *device) {
    if (device == NULL || device->operations == NULL || device->name[0] == 0) {
        return -1;
    }
    u64 flags;
    spin_lock_irqsave(&display_lock, &flags);
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] == device) {
            spin_unlock_irqrestore(&display_lock, flags);
            return -1;
        }
    }
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] == NULL) {
            device->registered = true;
            display_devices[index] = device;
            spin_unlock_irqrestore(&display_lock, flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&display_lock, flags);
    return -1;
}

int display_unregister(struct display_device *device) {
    u64 flags;
    spin_lock_irqsave(&display_lock, &flags);
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] == device) {
            display_devices[index] = NULL;
            device->registered = false;
            spin_unlock_irqrestore(&display_lock, flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&display_lock, flags);
    return -1;
}

struct display_device *display_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    u64 flags;
    spin_lock_irqsave(&display_lock, &flags);
    struct display_device *result = NULL;
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] != NULL && strcmp(display_devices[index]->name, name) == 0) {
            result = display_devices[index];
            break;
        }
    }
    spin_unlock_irqrestore(&display_lock, flags);
    return result;
}

struct display_device *display_primary(void) {
    u64 flags;
    spin_lock_irqsave(&display_lock, &flags);
    struct display_device *result = NULL;
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] != NULL) {
            result = display_devices[index];
            break;
        }
    }
    spin_unlock_irqrestore(&display_lock, flags);
    return result;
}

u32 display_count(void) {
    u64 flags;
    spin_lock_irqsave(&display_lock, &flags);
    u32 count = 0;
    for (u32 index = 0; index < DISPLAY_MAX_DEVICES; index++) {
        if (display_devices[index] != NULL) {
            count++;
        }
    }
    spin_unlock_irqrestore(&display_lock, flags);
    return count;
}
