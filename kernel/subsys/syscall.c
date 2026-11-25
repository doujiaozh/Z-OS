#include "syscall.h"
#include "console.h"
#include "fb.h"
#include "gdt.h"
#include "gfx.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"
#include "process.h"
#include "sched.h"
#include "string.h"
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
#define SYS_FB_INFO 9
#define SYS_FB_RECT 10
#define SYS_FB_PIXEL 11
#define SYS_MARK 12

#define SYSCALL_INT_INSTRUCTION_SIZE 2

struct fb_user_info {
    u64 physical;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bits_per_pixel;
    u32 format;
    u32 active;
};

static struct process *syscall_process(void) {
    return process_current();
}

static bool syscall_buffer_valid(u64 address, u64 length) {
    struct process *process = syscall_process();
    if (process != NULL) {
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
    if (length > 4096 || buffer == NULL || !syscall_buffer_valid((u64)buffer, length)) {
        return (u64)-1;
    }
    console_write_n(buffer, (usize)length);
    return length;
}

static u64 syscall_fork(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    u64 child_pid = 0;
    int result = process_fork_frame(process->pid, registers, &child_pid);
    if (result != PROCESS_ERROR_NONE) {
        return (u64)-1;
    }
    return child_pid;
}

static u64 syscall_wait(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    u64 target = registers->rdi;
    u64 status_address = registers->rsi;
    u64 reaped_pid = 0;
    u64 status = 0;
    int result = process_reap_child(process->pid, target, &reaped_pid, &status);
    if (result == PROCESS_ERROR_NONE) {
        if (status_address != 0) {
            if (!syscall_buffer_valid(status_address, sizeof(u64))) {
                return (u64)-1;
            }
            *(u64 *)status_address = status;
        }
        return reaped_pid;
    }
    if (result == PROCESS_ERROR_NO_CHILD) {
        return (u64)-1;
    }
    if (status_address != 0 && !syscall_buffer_valid(status_address, sizeof(u64))) {
        return (u64)-1;
    }
    process->wait_target = target;
    process->state = PROCESS_BLOCKED;
    registers->rip -= SYSCALL_INT_INSTRUCTION_SIZE;
    registers->rax = SYS_WAIT;
    return SYS_WAIT;
}

static u64 syscall_fb_info(struct registers *registers) {
    u64 destination = registers->rdi;
    if (!fb_mode_active() || destination == 0 ||
        !syscall_buffer_valid(destination, sizeof(struct fb_user_info))) {
        return (u64)-1;
    }
    struct fb_mode mode = fb_current_mode();
    struct fb_user_info info;
    memset(&info, 0, sizeof(info));
    info.physical = mode.physical;
    info.width = mode.width;
    info.height = mode.height;
    info.pitch = mode.pitch;
    info.bits_per_pixel = mode.bits_per_pixel;
    info.format = mode.format;
    info.active = 1;
    memcpy((void *)(usize)destination, &info, sizeof(info));
    return 0;
}

static u64 syscall_fb_rect(struct registers *registers) {
    if (registers->rsi > 0xFFFF || registers->rdi > 0xFFFF ||
        registers->rdx > 0xFFFF || registers->r10 > 0xFFFF) {
        return (u64)-1;
    }
    if (fb_fill((u32)registers->rdi, (u32)registers->rsi, (u32)registers->rdx,
                (u32)registers->r10, (u32)registers->r8) != 0) {
        return (u64)-1;
    }
    return 0;
}

static u64 syscall_fb_pixel(struct registers *registers) {
    if (registers->rdi > 0xFFFF || registers->rsi > 0xFFFF) {
        return (u64)-1;
    }
    if (!fb_mode_active()) {
        return (u64)-1;
    }
    return fb_pixel((u32)registers->rdi, (u32)registers->rsi);
}

void syscall_dispatch(struct registers *registers) {
    switch (registers->rax) {
        case SYS_WRITE:
            registers->rax = syscall_write(registers->rdi, (const char *)registers->rsi, registers->rdx);
            break;
        case SYS_EXIT:
            sched_exit_current(registers->rdi);
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
            if (syscall_process() != NULL) {
                syscall_process()->quantum = 0;
            }
            registers->rax = 0;
            break;
        case SYS_FORK:
            registers->rax = syscall_fork(registers);
            break;
        case SYS_EXEC: {
            u64 image_size = (u64)(user_program_end - user_program_start);
            int result = process_exec_current(user_program_start, image_size, registers);
            if (result != PROCESS_ERROR_NONE) {
                registers->rax = (u64)-1;
            }
            break;
        }
        case SYS_WAIT:
            registers->rax = syscall_wait(registers);
            break;
        case SYS_FB_INFO:
            registers->rax = syscall_fb_info(registers);
            break;
        case SYS_FB_RECT:
            registers->rax = syscall_fb_rect(registers);
            break;
        case SYS_FB_PIXEL:
            registers->rax = syscall_fb_pixel(registers);
            break;
        case SYS_MARK:
            sched_record_mark(registers->rdi);
            registers->rax = 0;
            break;
        default:
            registers->rax = (u64)-1;
            break;
    }
}
