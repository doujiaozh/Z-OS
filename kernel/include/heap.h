#ifndef Z_OS_HEAP_H
#define Z_OS_HEAP_H

#include "types.h"

struct heap_stats {
    u64 total_bytes;
    u64 used_bytes;
    u64 free_bytes;
    u64 allocation_count;
    u64 largest_free_block;
};

void heap_init(void);
void *kmalloc(usize size);
void *kzalloc(usize size);
void *krealloc(void *pointer, usize size);
void kfree(void *pointer);
struct heap_stats heap_get_stats(void);

#endif
