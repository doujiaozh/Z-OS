#ifndef Z_OS_KSTACK_H
#define Z_OS_KSTACK_H

#include "types.h"

#define KSTACK_WINDOW_BASE 0x60C00000ULL
#define KSTACK_SLOT_COUNT 32
#define KSTACK_MAX_PAGES 8
#define KSTACK_ERROR ((u64)0)

void kstack_init(void);
u64 kstack_alloc(u64 size);
bool kstack_free(u64 base);
u64 kstack_used(void);
u64 kstack_total(void);
u64 kstack_guard_address(u64 base);
bool kstack_guard_unmapped(u64 base);
bool kstack_is_managed(u64 base);
bool kstack_guard_fault(u64 address);

#endif
