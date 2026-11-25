#include "syscall.h"
#include "console.h"
#include "gdt.h"
#include "interrupts.h"
#include "process.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vmm.h"

#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_GETPID 3
#define SYS_UPTIME 4
#define SYS_YIELD 5
#define SYS_FORK 6
#define SYS_EXEC 7
#define SYS_WAIT 8

static bool syscall_user_buffer_valid(u64 address, u64 length) {
    struct process *process = process_find(process_current_pid());
    if (process != NULL && process->state == PROCESS_RUNNING) {
        return vmm_user_range_valid_in(&process->space, address, length);
    }
    return vmm_user_range_valid(address, length);
}

static u64 syscall_write(u64 descriptor, const char *buffer, u64 length) {
    if (descriptor != 1 && descriptor != 2) {
        return (u64)-1;
    }
    if (length == 0) {
        return 0;
    }
    if (length > 4096 || buffer == NULL || !syscall_user_buffer_valid((u64)buffer, length)) {
        return (u64)-1;
    }
    console_write_n(buffer, (usize)length);
    return length;
}

static u64 syscall_fork(void) {
    u64 parent_pid = process_current_pid();
    if (parent_pid == 0) {
        return (u64)-1;
    }
    u64 child_pid = 0;
    int result = process_fork(parent_pid, &child_pid);
    if (result != PROCESS_ERROR_NONE) {
        return (u64)-1;
    }
    return child_pid;
}

static u64 syscall_wait(u64 child_pid, u64 status_address) {
    u64 parent_pid = process_current_pid();
    if (parent_pid == 0) {
        return (u64)-1;
    }
    u64 status = 0;
    int result = process_wait(parent_pid, child_pid, &status);
    if (result <= 0) {
        return (u64)-1;
    }
    if (status_address != 0) {
        if (!syscall_user_buffer_valid(status_address, sizeof(u64))) {
            return (u64)-1;
        }
        *(u64 *)status_address = status;
    }
    return (u64)result;
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
            registers->rax = process_current_pid();
            if (registers->rax == 0) {
                registers->rax = sched_current_pid();
            }
            break;
        case SYS_UPTIME:
            registers->rax = timer_uptime_ms();
            break;
        case SYS_YIELD:
            registers->rax = 0;
            break;
        case SYS_FORK:
            registers->rax = syscall_fork();
            break;
        case SYS_EXEC: {
            u64 image_size = (u64)(user_program_end - user_program_start);
            u64 entry = 0;
            u64 stack = 0;
            int result = process_exec_current(user_program_start, image_size, &entry, &stack);
            if (result != PROCESS_ERROR_NONE) {
                registers->rax = (u64)-1;
            } else {
                registers->rip = entry;
                registers->rsp = stack;
                registers->cs = GDT_USER_CODE | 3;
                registers->ss = GDT_USER_DATA | 3;
                registers->rflags = 0x202;
                registers->rax = 0;
                registers->rbx = 0;
                registers->rcx = 0;
                registers->rdx = 0;
                registers->rsi = 0;
                registers->rdi = 0;
                registers->rbp = 0;
                registers->r8 = 0;
                registers->r9 = 0;
                registers->r10 = 0;
                registers->r11 = 0;
                registers->r12 = 0;
                registers->r13 = 0;
                registers->r14 = 0;
                registers->r15 = 0;
            }
            break;
        }
        case SYS_WAIT:
            registers->rax = syscall_wait(registers->rdi, registers->rsi);
            break;
        default:
            registers->rax = (u64)-1;
            break;
    }
}
