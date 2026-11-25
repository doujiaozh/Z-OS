#include "userlib.h"

static const char fork_start[] = "forktest: program started\n";
static const char fork_ok[] = "forktest: fork child resumed at parent code point\n";
static const char fork_wait_ok[] = "forktest: blocking wait returned child status\n";
static const char fork_fail[] = "forktest: validation failed\n";

static volatile u64 delay_counter;
static volatile u64 shared_value = 0x1111111111111111ULL;

static void delay(u64 iterations) {
    for (u64 index = 0; index < iterations; index++) {
        delay_counter++;
    }
}

__attribute__((noreturn)) void _start(void) {
    zwrite(fork_start);
    long child = zfork();
    if (child < 0) {
        zwrite(fork_fail);
        zexit(9);
    }
    if (child == 0) {
        zmark(0x21);
        shared_value = 0x2222222222222222ULL;
        delay(900000);
        if (shared_value != 0x2222222222222222ULL) {
            zmark(0x2F);
            zexit(7);
        }
        zmark(0x22);
        zexit(42);
    }
    zmark(0x20);
    if (shared_value != 0x1111111111111111ULL) {
        zmark(0x2F);
        zexit(8);
    }
    zmark(0x25);
    zmark(0x23);
    long status = -1;
    long reaped = zwait(child, (long)&status);
    zmark(0x24);
    if (reaped != child || status != 42) {
        zwrite(fork_fail);
        zexit(5);
    }
    zwrite(fork_ok);
    if (shared_value != 0x1111111111111111ULL) {
        zwrite(fork_fail);
        zexit(6);
    }
    zwrite(fork_wait_ok);
    zexit(0);
}
