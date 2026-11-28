#include "userlib.h"

#define PROBE_ADDRESS ((void *)0xFFFFFFFF80000000ULL)
#define PROBE_MARK_START 0x28
#define PROBE_MARK_ILLEGAL 0x29
#define PROBE_MARK_SURVIVED 0x2E

__attribute__((noreturn)) void _start(void) {
    zmark(PROBE_MARK_START);
    zwrite("kptiprobe: attempting kernel read\n");
    zmark(PROBE_MARK_ILLEGAL);
    volatile u64 value = *(volatile u64 *)PROBE_ADDRESS;
    zmark(PROBE_MARK_SURVIVED);
    zwrite("kptiprobe: kernel read succeeded\n");
    zexit(value == 0 ? 2 : 3);
    for (;;) {
    }
}
