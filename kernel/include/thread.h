#ifndef Z_OS_THREAD_H
#define Z_OS_THREAD_H

#include "types.h"

#define MAX_THREADS 32
#define THREAD_STACK_SIZE (32 * 1024)

enum thread_state_code {
    THREAD_UNUSED = 0,
    THREAD_READY = 1,
    THREAD_RUNNING = 2,
    THREAD_SLEEPING = 3,
    THREAD_ZOMBIE = 4
};

struct thread_status {
    u64 pid;
    char name[24];
    u32 state;
    u64 cpu_ticks;
    u64 stack_size;
};

typedef void (*thread_entry_t)(void *argument);

void sched_init(void);
u64 sched_create_thread(const char *name, thread_entry_t entry, void *argument);
void sched_start(void) __attribute__((noreturn));
void sched_tick(void);
void sched_yield(void);
void sched_sleep_ms(u64 milliseconds);
void sched_sleep_ticks(u64 ticks);
void sched_preempt_disable(void);
void sched_preempt_enable(void);
void sched_reap(void);
u64 sched_thread_count(void);
void sched_get_status(struct thread_status *buffer, u32 capacity, u32 *count);
const char *sched_current_name(void);
u64 sched_current_pid(void);

#endif
