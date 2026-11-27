#include "heap.h"
#include "io.h"
#include "lib.h"
#include "security.h"
#include "pmm.h"
#include "spinlock.h"
#include "string.h"
#include "vmm.h"

#define HEAP_BASE 0x60000000ULL
#define HEAP_SIZE (8ULL * 1024 * 1024)
#define HEAP_KASLR_STEP 0x00010000ULL
#define HEAP_KASLR_MAX_OFFSET (4ULL * 1024 * 1024)
static u64 heap_base_address = HEAP_BASE;
static u64 heap_kaslr_slot_value = 0;
#define HEAP_MAGIC 0x5A4F534845415042ULL
#define HEAP_ALIGNMENT 16

struct heap_block {
    u64 magic;
    usize size;
    struct heap_block *previous;
    struct heap_block *next;
    u64 used;
    u64 reserved;
};

static struct heap_block *heap_first = NULL;
static spinlock_t heap_lock = SPINLOCK_INIT;
static u64 heap_allocation_count = 0;

static usize align_up(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void block_initialize(struct heap_block *block, usize size, struct heap_block *previous, struct heap_block *next) {
    block->magic = HEAP_MAGIC;
    block->size = size;
    block->previous = previous;
    block->next = next;
    block->used = 0;
    block->reserved = 0;
}

static void split_block(struct heap_block *block, usize size) {
    if (block->size < size + sizeof(struct heap_block) + HEAP_ALIGNMENT) {
        return;
    }
    struct heap_block *remainder = (struct heap_block *)((u8 *)(block + 1) + size);
    block_initialize(remainder, block->size - size - sizeof(struct heap_block), block, block->next);
    if (block->next != NULL) {
        block->next->previous = remainder;
    }
    block->next = remainder;
    block->size = size;
}

u64 heap_base(void) {
    return heap_base_address;
}

u64 heap_kaslr_slot(void) {
    return heap_kaslr_slot_value;
}

bool heap_kaslr_enabled(void) {
    return heap_kaslr_slot_value != 0;
}

void heap_init(void) {
    u64 slots = HEAP_KASLR_MAX_OFFSET / HEAP_KASLR_STEP;
    u64 random = security_random();
    heap_kaslr_slot_value = random % slots;
    heap_base_address = HEAP_BASE + heap_kaslr_slot_value * HEAP_KASLR_STEP;
    for (u64 offset = 0; offset < HEAP_SIZE; offset += PAGE_SIZE) {
        u64 frame = pmm_alloc_frame();
        if (frame == 0) {
            panic("out of physical memory while building kernel heap");
        }
        if (vmm_map_page(heap_base_address + offset, frame,
                         PTE_PRESENT | PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            panic("failed to map kernel heap page");
        }
    }
    heap_first = (struct heap_block *)(usize)heap_base_address;
    block_initialize(heap_first, HEAP_SIZE - (heap_kaslr_slot_value * HEAP_KASLR_STEP) -
                     sizeof(struct heap_block), NULL, NULL);
    heap_allocation_count = 0;
    kprintf("Heap: %llu MiB at %p (kaslr slot %llu)\n", HEAP_SIZE / (1024 * 1024),
            (void *)heap_base_address, heap_kaslr_slot_value);
}

void *kmalloc(usize size) {
    if (size == 0) {
        return NULL;
    }
    usize needed = align_up(size, HEAP_ALIGNMENT);
    u64 flags;
    spin_lock_irqsave(&heap_lock, &flags);
    struct heap_block *block = heap_first;
    while (block != NULL) {
        if (!block->used && block->size >= needed) {
            split_block(block, needed);
            block->used = 1;
            heap_allocation_count++;
            spin_unlock_irqrestore(&heap_lock, flags);
            return (void *)(block + 1);
        }
        block = block->next;
    }
    spin_unlock_irqrestore(&heap_lock, flags);
    return NULL;
}

void *kzalloc(usize size) {
    void *pointer = kmalloc(size);
    if (pointer != NULL) {
        memset(pointer, 0, size);
    }
    return pointer;
}

void *krealloc(void *pointer, usize size) {
    if (pointer == NULL) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(pointer);
        return NULL;
    }
    struct heap_block *block = (struct heap_block *)pointer - 1;
    if (block->magic != HEAP_MAGIC) {
        return NULL;
    }
    if (block->size >= size) {
        return pointer;
    }
    void *replacement = kmalloc(size);
    if (replacement == NULL) {
        return NULL;
    }
    memcpy(replacement, pointer, block->size);
    kfree(pointer);
    return replacement;
}

void kfree(void *pointer) {
    if (pointer == NULL) {
        return;
    }
    struct heap_block *block = (struct heap_block *)pointer - 1;
    if (block->magic != HEAP_MAGIC) {
        panic("corrupt heap block");
    }
    u64 flags;
    spin_lock_irqsave(&heap_lock, &flags);
    if (!block->used) {
        spin_unlock_irqrestore(&heap_lock, flags);
        panic("double free detected");
    }
    block->used = 0;
    if (block->next != NULL && !block->next->used && block->next->magic == HEAP_MAGIC) {
        struct heap_block *next = block->next;
        block->size += sizeof(struct heap_block) + next->size;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->previous = block;
        }
    }
    if (block->previous != NULL && !block->previous->used && block->previous->magic == HEAP_MAGIC) {
        struct heap_block *previous = block->previous;
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;
        if (previous->next != NULL) {
            previous->next->previous = previous;
        }
    }
    spin_unlock_irqrestore(&heap_lock, flags);
}

struct heap_stats heap_get_stats(void) {
    u64 flags;
    spin_lock_irqsave(&heap_lock, &flags);
    struct heap_stats stats = {0};
    stats.total_bytes = HEAP_SIZE - (heap_kaslr_slot_value * HEAP_KASLR_STEP);
    struct heap_block *block = heap_first;
    while (block != NULL) {
        if (block->used) {
            stats.used_bytes += block->size;
        } else {
            stats.free_bytes += block->size;
            if (block->size > stats.largest_free_block) {
                stats.largest_free_block = block->size;
            }
        }
        block = block->next;
    }
    stats.allocation_count = heap_allocation_count;
    spin_unlock_irqrestore(&heap_lock, flags);
    return stats;
}
