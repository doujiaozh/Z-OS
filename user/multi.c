#include "userlib.h"

static const char start_message[] = "multi: process started\n";
static const char done_message[] = "multi: process finished\n";

static volatile u64 delay_counter;

static void delay(u64 iterations) {
    for (u64 index = 0; index < iterations; index++) {
        delay_counter++;
    }
}

__attribute__((noreturn)) void _start(void) {
    zwrite(start_message);
    for (u32 round = 0; round < 12; round++) {
        delay(600000);
        zmark(0x10);
    }
    zyield();
    delay(200000);
    zmark(0x11);
    zwrite(done_message);
    zexit(0);
}
