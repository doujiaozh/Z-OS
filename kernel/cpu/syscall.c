#include "syscall.h"
#include "console.h"
#include "interrupts.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vmm.h"

#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_GETPID 3
#define SYS_UPTIME 4
#define SYS_YIELD 5

static u64 syscall_write(u64 descriptor, const char *buffer, u64 length) {
    if (descriptor != 1 && descriptor != 2) {
        return (u64)-1;
    }
    if (length == 0) {
        return 0;
    }
    if (length > 4096 || buffer == NULL || !vmm_user_range_valid((u64)buffer, length)) {
        return (u64)-1;
    }
    console_write_n(buffer, (usize)length);
    return length;
}

void syscall_dispatch(struct registers *registers) {
    switch (registers->rax) {
        case SYS_WRITE:
            registers->rax = syscall_write(registers->rdi, (const char *)registers->rsi, registers->rdx);
            break;
        case SYS_EXIT:
            if ((registers->cs & 3) == 3) {
                user_finish(registers->rdi);
            }
            registers->rax = (u64)-1;
            break;
        case SYS_GETPID:
            registers->rax = sched_current_pid();
            break;
        case SYS_UPTIME:
            registers->rax = timer_uptime_ms();
            break;
        case SYS_YIELD:
            registers->rax = 0;
            break;
        default:
            registers->rax = (u64)-1;
            break;
    }
}
