#ifndef Z_OS_SPINLOCK_H
#define Z_OS_SPINLOCK_H

#include "types.h"
#include "io.h"

typedef struct {
    volatile int value;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock_irqsave(spinlock_t *lock, u64 *flags) {
    *flags = irq_save();
    while (__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED)) {
            cpu_pause();
        }
    }
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, u64 flags) {
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
    irq_restore(flags);
}

#endif
