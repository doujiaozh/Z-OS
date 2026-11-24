#include "pmm.h"
#include "boot.h"
#include "io.h"
#include "lib.h"
#include "spinlock.h"
#include "string.h"

extern u8 __kernel_start[];
extern u8 __kernel_end[];

static u8 frame_bitmap[PMM_BITMAP_BYTES] aligned(16);
static u64 total_frames = 0;
static u64 free_frames = 0;
static u64 search_hint = 0;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static void bitmap_set_used(u64 frame) {
    frame_bitmap[frame / 8] |= (u8)(1 << (frame % 8));
}

static void bitmap_set_free(u64 frame) {
    frame_bitmap[frame / 8] &= (u8)~(1 << (frame % 8));
}

static bool bitmap_is_used(u64 frame) {
    return (frame_bitmap[frame / 8] & (u8)(1 << (frame % 8))) != 0;
}

static void mark_range_used(u64 start, u64 end) {
    if (end > PMM_MAX_PHYS) {
        end = PMM_MAX_PHYS;
    }
    if (start >= end) {
        return;
    }
    u64 first = (start + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 last = end / PAGE_SIZE;
    for (u64 frame = first; frame < last; frame++) {
        if (!bitmap_is_used(frame)) {
            bitmap_set_used(frame);
            if (free_frames > 0) {
                free_frames--;
            }
        }
    }
}

static void mark_range_free(u64 start, u64 end) {
    if (end > PMM_MAX_PHYS) {
        end = PMM_MAX_PHYS;
    }
    start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end &= ~(PAGE_SIZE - 1);
    if (start >= end) {
        return;
    }
    for (u64 frame = start / PAGE_SIZE; frame < end / PAGE_SIZE; frame++) {
        if (bitmap_is_used(frame)) {
            bitmap_set_free(frame);
            free_frames++;
        }
    }
}

void pmm_init(struct boot_info *boot) {
    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    total_frames = 0;
    free_frames = 0;
    search_hint = 0;

    if (boot == NULL || boot->magic != BOOTINFO_MAGIC) {
        panic("physical memory manager requires a valid boot info block");
    }

    for (u32 index = 0; index < boot->entry_count; index++) {
        struct e820_entry *entry = &boot->entries[index];
        if (entry->type != E820_USABLE) {
            continue;
        }
        u64 start = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        u64 end = (entry->base + entry->length) & ~(PAGE_SIZE - 1);
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }
        if (start < end) {
            total_frames += (end - start) / PAGE_SIZE;
            mark_range_free(start, end);
        }
    }

    mark_range_used(0, 0x100000);
    u64 kernel_end = ((u64)__kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mark_range_used((u64)__kernel_start, kernel_end);

    if (total_frames < free_frames) {
        panic("physical memory accounting overflow");
    }

    kprintf("PMM: %llu MiB usable, %llu MiB free, low reserve 1024 KiB\n",
            total_frames * 4 / 1024, free_frames * 4 / 1024);
}

static u64 find_free_frame_locked(void) {
    if (free_frames == 0) {
        return 0;
    }
    u64 start_word = search_hint / 64;
    u64 word_count = PMM_MAX_FRAMES / 64;
    for (u64 offset = 0; offset < word_count; offset++) {
        u64 word_index = (start_word + offset) % word_count;
        u64 word = ~((u64 *)frame_bitmap)[word_index];
        if (word == 0) {
            continue;
        }
        u64 bit = 0;
        while (((word >> bit) & 1) == 0) {
            bit++;
        }
        u64 frame = word_index * 64 + bit;
        if (frame < PMM_MAX_FRAMES && !bitmap_is_used(frame)) {
            return frame;
        }
    }
    return 0;
}

u64 pmm_alloc_frame(void) {
    u64 flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    u64 frame = find_free_frame_locked();
    if (frame == 0) {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }
    bitmap_set_used(frame);
    free_frames--;
    search_hint = frame + 1;
    spin_unlock_irqrestore(&pmm_lock, flags);
    u64 address = frame * PAGE_SIZE;
    memset((void *)address, 0, PAGE_SIZE);
    return address;
}

void pmm_free_frame(u64 frame) {
    if (frame == 0 || (frame & (PAGE_SIZE - 1)) != 0 || frame >= PMM_MAX_PHYS) {
        return;
    }
    u64 index = frame / PAGE_SIZE;
    u64 flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (bitmap_is_used(index)) {
        bitmap_set_free(index);
        free_frames++;
        if (index < search_hint) {
            search_hint = index;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

u64 pmm_alloc_contiguous(u32 count) {
    if (count == 0) {
        return 0;
    }
    u64 flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    u64 run = 0;
    u64 run_start = 0;
    for (u64 frame = 1; frame < PMM_MAX_FRAMES; frame++) {
        if (!bitmap_is_used(frame)) {
            if (run == 0) {
                run_start = frame;
            }
            run++;
            if (run == count) {
                for (u64 index = run_start; index < run_start + count; index++) {
                    bitmap_set_used(index);
                    free_frames--;
                }
                search_hint = run_start + count;
                spin_unlock_irqrestore(&pmm_lock, flags);
                u64 address = run_start * PAGE_SIZE;
                memset((void *)address, 0, count * PAGE_SIZE);
                return address;
            }
        } else {
            run = 0;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    return 0;
}

void pmm_free_contiguous(u64 base, u32 count) {
    for (u32 index = 0; index < count; index++) {
        pmm_free_frame(base + index * PAGE_SIZE);
    }
}

struct pmm_stats pmm_get_stats(void) {
    u64 flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    struct pmm_stats stats;
    stats.total_frames = total_frames;
    stats.free_frames = free_frames;
    stats.used_frames = total_frames - free_frames;
    spin_unlock_irqrestore(&pmm_lock, flags);
    return stats;
}

bool pmm_frame_is_used(u64 frame) {
    if (frame >= PMM_MAX_FRAMES) {
        return true;
    }
    u64 flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    bool used = bitmap_is_used(frame);
    spin_unlock_irqrestore(&pmm_lock, flags);
    return used;
}
