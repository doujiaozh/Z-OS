#include "kstack.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

struct kstack_slot {
    bool used;
    u32 pages;
    u64 base;
};

static struct kstack_slot slots[KSTACK_SLOT_COUNT];
static u64 used_count = 0;

static u64 slot_guard_address(u32 index) {
    return KSTACK_WINDOW_BASE + (u64)index * (KSTACK_MAX_PAGES + 1) * PAGE_SIZE;
}

void kstack_init(void) {
    memset(slots, 0, sizeof(slots));
    used_count = 0;
}

u64 kstack_alloc(u64 size) {
    if (size == 0) {
        return KSTACK_ERROR;
    }
    u64 pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) {
        pages = 1;
    }
    if (pages > KSTACK_MAX_PAGES) {
        return KSTACK_ERROR;
    }
    for (u32 index = 0; index < KSTACK_SLOT_COUNT; index++) {
        if (slots[index].used) {
            continue;
        }
        u64 guard = slot_guard_address(index);
        u64 base = guard + PAGE_SIZE;
        u64 frames[KSTACK_MAX_PAGES];
        bool mapped_all = true;
        for (u64 page = 0; page < pages; page++) {
            u64 frame = pmm_alloc_frame();
            if (frame == 0) {
                mapped_all = false;
                for (u64 undo = 0; undo < page; undo++) {
                    vmm_unmap_page(base + undo * PAGE_SIZE);
                    pmm_free_frame(frames[undo]);
                }
                break;
            }
            frames[page] = frame;
            memset((void *)(usize)frame, 0, PAGE_SIZE);
            if (vmm_map_page(base + page * PAGE_SIZE, frame,
                             PTE_PRESENT | PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
                pmm_free_frame(frame);
                for (u64 undo = 0; undo < page; undo++) {
                    vmm_unmap_page(base + undo * PAGE_SIZE);
                    pmm_free_frame(frames[undo]);
                }
                mapped_all = false;
                break;
            }
        }
        if (!mapped_all) {
            continue;
        }
        slots[index].used = true;
        slots[index].pages = (u32)pages;
        slots[index].base = base;
        used_count++;
        return base;
    }
    return KSTACK_ERROR;
}

bool kstack_free(u64 base) {
    if (base == 0) {
        return false;
    }
    for (u32 index = 0; index < KSTACK_SLOT_COUNT; index++) {
        if (!slots[index].used || slots[index].base != base) {
            continue;
        }
        for (u32 page = 0; page < slots[index].pages; page++) {
            u64 virtual_address = base + (u64)page * PAGE_SIZE;
            u64 frame = vmm_get_physical(virtual_address);
            vmm_unmap_page(virtual_address);
            if (frame != 0) {
                pmm_free_frame(frame);
            }
        }
        memset(&slots[index], 0, sizeof(struct kstack_slot));
        if (used_count > 0) {
            used_count--;
        }
        return true;
    }
    return false;
}

u64 kstack_used(void) {
    return used_count;
}

u64 kstack_total(void) {
    return KSTACK_SLOT_COUNT;
}

u64 kstack_guard_address(u64 base) {
    return base >= PAGE_SIZE ? base - PAGE_SIZE : 0;
}

bool kstack_guard_unmapped(u64 base) {
    if (base < PAGE_SIZE) {
        return false;
    }
    return vmm_get_physical(base - PAGE_SIZE) == 0;
}

bool kstack_is_managed(u64 base) {
    for (u32 index = 0; index < KSTACK_SLOT_COUNT; index++) {
        if (slots[index].used && slots[index].base == base) {
            return true;
        }
    }
    return false;
}
