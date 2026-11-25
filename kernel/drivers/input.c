#include "input.h"
#include "io.h"

#define INPUT_BUFFER_SIZE 256

static volatile char input_buffer[INPUT_BUFFER_SIZE];
static volatile u32 input_head = 0;
static volatile u32 input_tail = 0;

void input_init(void) {
    input_head = 0;
    input_tail = 0;
}

void input_push(char character) {
    u32 head = input_head;
    u32 next = (head + 1) % INPUT_BUFFER_SIZE;
    if (next == input_tail) {
        return;
    }
    input_buffer[head] = character;
    __atomic_store_n(&input_head, next, __ATOMIC_RELEASE);
}

bool input_available(void) {
    return __atomic_load_n(&input_head, __ATOMIC_ACQUIRE) != __atomic_load_n(&input_tail, __ATOMIC_RELAXED);
}

int input_getchar(void) {
    for (;;) {
        u64 flags = irq_save();
        u32 tail = input_tail;
        if (tail != __atomic_load_n(&input_head, __ATOMIC_ACQUIRE)) {
            char character = input_buffer[tail];
            __atomic_store_n(&input_tail, (tail + 1) % INPUT_BUFFER_SIZE, __ATOMIC_RELEASE);
            irq_restore(flags);
            return (u8)character;
        }
        __asm__ volatile("sti; hlt; cli" : : : "memory");
        irq_restore(flags);
    }
}

int input_trygetchar(void) {
    u64 flags = irq_save();
    u32 tail = input_tail;
    if (tail != __atomic_load_n(&input_head, __ATOMIC_ACQUIRE)) {
        char character = input_buffer[tail];
        __atomic_store_n(&input_tail, (tail + 1) % INPUT_BUFFER_SIZE, __ATOMIC_RELEASE);
        irq_restore(flags);
        return (u8)character;
    }
    irq_restore(flags);
    return -1;
}

void input_flush(void) {
    __atomic_store_n(&input_tail, __atomic_load_n(&input_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
}
