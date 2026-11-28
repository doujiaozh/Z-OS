#ifndef Z_OS_KSTACK_H
#define Z_OS_KSTACK_H

#include "types.h"

#define KSTACK_WINDOW_BASE 0xFFFFFFC040000000ULL
#define KSTACK_WINDOW_SIZE 0x00200000ULL
#define KSTACK_ENTRY_GUARD_OFFSET 0x0000ULL
#define KSTACK_ENTRY_STACK_OFFSET 0x1000ULL
#define KSTACK_ENTRY_STACK_PAGES 4
#define KSTACK_FRAME_PAGE_OFFSET 0x5000ULL
#define KSTACK_SLOT_AREA_OFFSET 0x9000ULL
#define KSTACK_SLOT_STRIDE_PAGES 10
#define KSTACK_SLOT_COUNT 32
#define KSTACK_MAX_PAGES 8
#define KSTACK_WINDOW_PDPT_SLOT 257
#define KSTACK_PDPT_INDEX 257
#define KSTACK_PD_INDEX 384
#define KSTACK_PAGE_TABLE_INDEX 512
#define KSTACK_ERROR ((u64)0)

#define KSTACK_ENTRY_STACK_TOP (KSTACK_WINDOW_BASE + KSTACK_FRAME_PAGE_OFFSET)
#define KSTACK_FRAME_PAGE (KSTACK_WINDOW_BASE + KSTACK_FRAME_PAGE_OFFSET)
#define KSTACK_FRAME_PAGE_END (KSTACK_FRAME_PAGE + 0x1000ULL)

void kstack_init(void);
u64 kstack_alloc(u64 size);
bool kstack_free(u64 base);
u64 kstack_used(void);
u64 kstack_total(void);
u64 kstack_guard_address(u64 base);
bool kstack_guard_unmapped(u64 base);
bool kstack_is_managed(u64 base);
bool kstack_guard_fault(u64 address);
u64 kstack_window_pd(void);
bool kstack_window_ready(void);
u64 kstack_entry_stack_top(void);
u64 kstack_frame_page(void);
u64 kstack_window_end(void);
u64 kstack_slot_base(u32 index);

#endif
