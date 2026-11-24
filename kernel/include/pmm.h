#ifndef Z_OS_PMM_H
#define Z_OS_PMM_H

#include "types.h"

#define PMM_MAX_PHYS 0x40000000ULL
#define PMM_MAX_FRAMES (PMM_MAX_PHYS / PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_FRAMES / 8)

struct boot_info;

struct pmm_stats {
    u64 total_frames;
    u64 free_frames;
    u64 used_frames;
};

void pmm_init(struct boot_info *boot);
u64 pmm_alloc_frame(void);
void pmm_free_frame(u64 frame);
u64 pmm_alloc_contiguous(u32 count);
void pmm_free_contiguous(u64 base, u32 count);
struct pmm_stats pmm_get_stats(void);
bool pmm_frame_is_used(u64 frame);

#endif
