#include "task.h"
#include "thread.h"
#include "gdt.h"
#include "heap.h"
#include "io.h"
#include "lib.h"
#include "sched.h"
#include "string.h"
#include "timer.h"

struct thread {
    struct task_base task;
    u64 rsp;
    u64 stack_base;
    u64 stack_top;
    usize stack_size;
    u64 pid;
    char name[24];
    volatile u32 state;
    u64 wake_tick;
    u64 cpu_ticks;
    thread_entry_t entry;
    void *argument;
};

extern u8 boot_stack[];
extern u8 boot_stack_top[];
extern void context_switch(u64 *old_stack, u64 new_stack);
extern void thread_trampoline(void);

static struct thread threads[MAX_THREADS];
static struct thread *current_thread = NULL;
static u32 thread_count = 0;
static u64 next_pid = 1;
static u64 current_slot = 0;
static volatile u32 preempt_depth = 0;

void sched_thread_bootstrap(void);
void sched_thread_exit(void) __attribute__((noreturn));

static struct thread *pick_next(void) {
    if (thread_count == 0) {
        return NULL;
    }
    u32 start = (u32)(current_slot + 1) % thread_count;
    for (u32 offset = 0; offset < thread_count; offset++) {
        u32 index = (start + offset) % thread_count;
        if (threads[index].state == THREAD_READY) {
            return &threads[index];
        }
    }
    return NULL;
}

static void switch_to(struct thread *next) {
    struct thread *previous = current_thread;
    if (next == NULL || next == previous) {
        return;
    }
    if (previous->state == THREAD_RUNNING) {
        previous->state = THREAD_READY;
    }
    next->state = THREAD_RUNNING;
    current_thread = next;
    current_slot = (u64)(next - threads);
    tss_set_rsp0(next->stack_top);
    context_switch(&previous->rsp, next->rsp);
}

void sched_init(void) {
    memset(threads, 0, sizeof(threads));
    thread_count = 1;
    next_pid = 1;
    current_slot = 0;
    struct thread *init = &threads[0];
    init->rsp = 0;
    init->stack_base = (u64)boot_stack;
    init->stack_top = (u64)boot_stack_top;
    init->stack_size = 65536;
    init->pid = 0;
    strncpy(init->name, "init", sizeof(init->name) - 1);
    init->state = THREAD_RUNNING;
    init->entry = NULL;
    init->argument = NULL;
    current_thread = init;
    tss_set_rsp0(init->stack_top);
    kprintf("Scheduler: round-robin preemptive, quantum %u ms\n", 1000 / TIMER_FREQUENCY);
}

static bool initialize_thread_stack(struct thread *thread) {
    u64 top = (thread->stack_base + thread->stack_size) & ~0xFULL;
    thread->stack_top = top;
    u64 *stack = (u64 *)top;
    *--stack = (u64)thread_trampoline;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    thread->rsp = (u64)stack;
    return true;
}

u64 sched_create_thread(const char *name, thread_entry_t entry, void *argument) {
    if (entry == NULL) {
        return 0;
    }
    u64 flags = irq_save();
    u32 slot = MAX_THREADS;
    for (u32 index = 1; index < MAX_THREADS; index++) {
        if (threads[index].state == THREAD_UNUSED) {
            slot = index;
            break;
        }
    }
    if (slot == MAX_THREADS) {
        irq_restore(flags);
        return 0;
    }
    struct thread *thread = &threads[slot];
    memset(thread, 0, sizeof(*thread));
    thread->stack_size = THREAD_STACK_SIZE;
    thread->stack_base = (u64)kmalloc(THREAD_STACK_SIZE);
    if (thread->stack_base == 0) {
        thread->state = THREAD_UNUSED;
        irq_restore(flags);
        return 0;
    }
    thread->pid = next_pid++;
    strncpy(thread->name, name, sizeof(thread->name) - 1);
    thread->entry = entry;
    thread->argument = argument;
    thread->state = THREAD_READY;
    initialize_thread_stack(thread);
    if (slot + 1 > thread_count) {
        thread_count = slot + 1;
    }
    irq_restore(flags);
    return thread->pid;
}

void sched_thread_bootstrap(void) {
    interrupts_enable();
    struct thread *thread = current_thread;
    if (thread != NULL && thread->entry != NULL) {
        thread->entry(thread->argument);
    }
}

void sched_thread_exit(void) {
    u64 flags = irq_save();
    if (current_thread == NULL || current_thread == &threads[0]) {
        panic("init thread attempted to exit");
    }
    current_thread->state = THREAD_ZOMBIE;
    struct thread *next = pick_next();
    if (next == NULL) {
        panic("no runnable thread during exit");
    }
    switch_to(next);
    (void)flags;
    panic("zombie thread resumed");
}

void sched_tick(void) {
    if (current_thread != NULL) {
        current_thread->cpu_ticks++;
    }
    u64 now = timer_ticks();
    for (u32 index = 0; index < thread_count; index++) {
        if (threads[index].state == THREAD_SLEEPING && now >= threads[index].wake_tick) {
            threads[index].state = THREAD_READY;
        }
    }
    if (preempt_depth > 0 || current_thread == NULL || sched_user_active()) {
        return;
    }
    struct thread *next = pick_next();
    if (next != NULL) {
        switch_to(next);
    }
}

void sched_yield(void) {
    u64 flags = irq_save();
    if (current_thread == NULL || current_thread == &threads[0]) {
        irq_restore(flags);
        return;
    }
    if (current_thread->state == THREAD_RUNNING) {
        current_thread->state = THREAD_READY;
    }
    struct thread *next = pick_next();
    if (next != NULL) {
        switch_to(next);
    }
    irq_restore(flags);
}

void sched_sleep_ticks(u64 ticks) {
    if (ticks == 0) {
        sched_yield();
        return;
    }
    u64 flags = irq_save();
    if (current_thread == NULL) {
        irq_restore(flags);
        return;
    }
    current_thread->wake_tick = timer_ticks() + ticks;
    current_thread->state = THREAD_SLEEPING;
    struct thread *next = pick_next();
    if (next == NULL) {
        current_thread->state = THREAD_RUNNING;
        irq_restore(flags);
        return;
    }
    switch_to(next);
    irq_restore(flags);
}

bool sched_in_thread_context(void) {
    return current_thread != NULL;
}

void sched_sleep_ms(u64 milliseconds) {
    u64 frequency = timer_frequency();
    u64 ticks = (milliseconds * frequency + 999) / 1000;
    if (ticks == 0) {
        ticks = 1;
    }
    sched_sleep_ticks(ticks);
}

void sched_preempt_disable(void) {
    u64 flags = irq_save();
    preempt_depth++;
    irq_restore(flags);
}

void sched_preempt_enable(void) {
    u64 flags = irq_save();
    if (preempt_depth > 0) {
        preempt_depth--;
    }
    irq_restore(flags);
}

u64 sched_thread_count(void) {
    return thread_count;
}

void sched_get_status(struct thread_status *buffer, u32 capacity, u32 *count) {
    u64 flags = irq_save();
    u32 written = 0;
    for (u32 index = 0; index < thread_count && written < capacity; index++) {
        if (threads[index].state == THREAD_UNUSED) {
            continue;
        }
        buffer[written].pid = threads[index].pid;
        strncpy(buffer[written].name, threads[index].name, sizeof(buffer[written].name) - 1);
        buffer[written].name[sizeof(buffer[written].name) - 1] = 0;
        buffer[written].state = threads[index].state;
        buffer[written].cpu_ticks = threads[index].cpu_ticks;
        buffer[written].stack_size = threads[index].stack_size;
        written++;
    }
    if (count != NULL) {
        *count = written;
    }
    irq_restore(flags);
}

const char *sched_current_name(void) {
    return current_thread != NULL ? current_thread->name : "none";
}

u64 sched_current_pid(void) {
    return current_thread != NULL ? current_thread->pid : 0;
}

void sched_reap(void) {
    u64 flags = irq_save();
    for (u32 index = 1; index < thread_count; index++) {
        if (threads[index].state == THREAD_ZOMBIE) {
            if (threads[index].stack_base != 0) {
                kfree((void *)(usize)threads[index].stack_base);
            }
            memset(&threads[index], 0, sizeof(threads[index]));
        }
    }
    while (thread_count > 1 && threads[thread_count - 1].state == THREAD_UNUSED) {
        thread_count--;
    }
    irq_restore(flags);
}

void sched_start(void) {
    for (;;) {
        interrupts_enable();
        sched_reap();
        cpu_halt();
    }
}
