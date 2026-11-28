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
static u64 window_pd_physical = 0;
static u64 window_pt_physical = 0;
static u64 window_pt_virtual = 0;

static u64 frame_for_slot(u64 slot_index) {
    return pmm_alloc_frame();
}

u64 kstack_slot_base(u32 index) {
    return KSTACK_WINDOW_BASE + KSTACK_SLOT_AREA_OFFSET +
           (u64)index * KSTACK_SLOT_STRIDE_PAGES * PAGE_SIZE;
}

static int map_window_page(u64 offset, u64 frame, u64 flags) {
    u64 page_index = offset / PAGE_SIZE;
    if (page_index >= KSTACK_PAGE_TABLE_INDEX) {
        return -1;
    }
    if (frame == 0) {
        return -1;
    }
    memset((void *)(usize)frame, 0, PAGE_SIZE);
    u64 *page_table = (u64 *)window_pt_virtual;
    page_table[page_index] = (frame & PTE_ADDRESS) |
                             (flags & (0xFFF | PTE_NO_EXECUTE)) | PTE_PRESENT;
    return 0;
}

void kstack_init(void) {
    memset(slots, 0, sizeof(slots));
    used_count = 0;
    window_pd_physical = pmm_alloc_frame();
    window_pt_physical = pmm_alloc_frame();
    if (window_pd_physical == 0 || window_pt_physical == 0) {
        window_pd_physical = 0;
        window_pt_physical = 0;
        return;
    }
    memset((void *)(usize)window_pd_physical, 0, PAGE_SIZE);
    memset((void *)(usize)window_pt_physical, 0, PAGE_SIZE);
    window_pt_virtual = window_pt_physical;
    u64 *window_pd = (u64 *)window_pd_physical;
    window_pd[0] = window_pt_physical | PTE_PRESENT | PTE_WRITABLE;
    vmm_attach_kstack_window(window_pd_physical);
    u64 entry_frames[KSTACK_ENTRY_STACK_PAGES];
    for (u32 index = 0; index < KSTACK_ENTRY_STACK_PAGES; index++) {
        entry_frames[index] = frame_for_slot(index);
        if (entry_frames[index] == 0) {
            return;
        }
        if (map_window_page(KSTACK_ENTRY_STACK_OFFSET + (u64)index * PAGE_SIZE,
                            entry_frames[index], PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return;
        }
    }
    u64 frame_page = frame_for_slot(0);
    if (frame_page == 0) {
        return;
    }
    if (map_window_page(KSTACK_FRAME_PAGE_OFFSET, frame_page,
                        PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
        return;
    }
}

u64 kstack_alloc(u64 size) {
    if (size == 0 || window_pd_physical == 0) {
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
        u64 base = kstack_slot_base(index);
        u64 guard_offset = (base - KSTACK_WINDOW_BASE) - PAGE_SIZE;
        u64 frames[KSTACK_MAX_PAGES];
        bool mapped_all = true;
        for (u64 page = 0; page < pages; page++) {
            u64 frame = frame_for_slot(page);
            if (frame == 0) {
                mapped_all = false;
                break;
            }
            frames[page] = frame;
            u64 offset = (base - KSTACK_WINDOW_BASE) + page * PAGE_SIZE;
            if (map_window_page(offset, frame, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
                pmm_free_frame(frame);
                mapped_all = false;
                break;
            }
        }
        if (!mapped_all) {
            continue;
        }
        (void)guard_offset;
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
        u64 *page_table = (u64 *)window_pt_virtual;
        for (u32 page = 0; page < slots[index].pages; page++) {
            u64 offset = (base - KSTACK_WINDOW_BASE) + (u64)page * PAGE_SIZE;
            u64 page_index = offset / PAGE_SIZE;
            u64 frame = page_table[page_index] & PTE_ADDRESS;
            page_table[page_index] = 0;
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
    if (base < PAGE_SIZE || window_pt_virtual == 0) {
        return false;
    }
    u64 offset = (base - KSTACK_WINDOW_BASE) - PAGE_SIZE;
    u64 page_index = offset / PAGE_SIZE;
    if (page_index >= KSTACK_PAGE_TABLE_INDEX) {
        return false;
    }
    u64 *page_table = (u64 *)window_pt_virtual;
    return (page_table[page_index] & PTE_PRESENT) == 0;
}

bool kstack_is_managed(u64 base) {
    for (u32 index = 0; index < KSTACK_SLOT_COUNT; index++) {
        if (slots[index].used && slots[index].base == base) {
            return true;
        }
    }
    return false;
}

bool kstack_guard_fault(u64 address) {
    if (address < KSTACK_WINDOW_BASE || address >= kstack_window_end()) {
        return false;
    }
    u64 offset = address - KSTACK_WINDOW_BASE;
    if (offset < KSTACK_SLOT_AREA_OFFSET - PAGE_SIZE) {
        return false;
    }
    u64 stride = (u64)KSTACK_SLOT_STRIDE_PAGES * PAGE_SIZE;
    u64 relative = offset - (KSTACK_SLOT_AREA_OFFSET - PAGE_SIZE);
    if (relative >= stride * KSTACK_SLOT_COUNT) {
        return false;
    }
    return (relative % stride) == 0;
}

u64 kstack_window_pd(void) {
    return window_pd_physical;
}

u64 kstack_entry_stack_top(void) {
    return KSTACK_ENTRY_STACK_TOP;
}

u64 kstack_frame_page(void) {
    return KSTACK_FRAME_PAGE;
}

u64 kstack_window_end(void) {
    return KSTACK_WINDOW_BASE + KSTACK_WINDOW_SIZE;
}

bool kstack_window_ready(void) {
    return window_pd_physical != 0 && window_pt_virtual != 0;
}
