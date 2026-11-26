#include "syscall.h"
#include "console.h"
#include "cred.h"
#include "fb.h"
#include "fd.h"
#include "gdt.h"
#include "gfx.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"
#include "mouse.h"
#include "process.h"
#include "sched.h"
#include "security.h"
#include "signal.h"
#include "dns.h"
#include "socket.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"
#include "vmm.h"
#include "waitq.h"

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
#define SYS_OPEN 13
#define SYS_CLOSE 14
#define SYS_READ 15
#define SYS_GETUID 16
#define SYS_GETEUID 17
#define SYS_SIGNAL 18
#define SYS_KILL 19
#define SYS_SIGRETURN 20
#define SYS_SIGPROCMASK 21
#define SYS_DUP 22
#define SYS_FB_TEXT 23
#define SYS_MOUSE 24
#define SYS_GUI_ENTER 25
#define SYS_GUI_EXIT 26
#define SYS_SOCKET 27
#define SYS_BIND 28
#define SYS_SENDTO 29
#define SYS_RECVFROM 30
#define SYS_SOCKET_CLOSE 31
#define SYS_CONNECT 32
#define SYS_ACCEPT 33
#define SYS_RECV 34
#define SYS_SEND 35
#define SYS_DNS 36
#define SYS_GET_TEST_PORT 37
#define SYS_SELECT 38
#define SYS_SET_NONBLOCKING 39

static bool gui_enter_flag = false;
static bool gui_exit_flag = false;

void gui_request_enter(void) {
    gui_enter_flag = true;
    gui_exit_flag = false;
}

void gui_request_exit(void) {
    gui_exit_flag = true;
    gui_enter_flag = false;
}

bool gui_enter_requested(void) {
    return gui_enter_flag;
}

bool gui_exit_requested(void) {
    return gui_exit_flag;
}

void gui_clear_requests(void) {
    gui_enter_flag = false;
    gui_exit_flag = false;
}

static u32 test_tcp_port = 34567;

void syscall_set_test_port(u32 port) {
    test_tcp_port = port == 0 ? 34567 : port;
}

u32 syscall_get_test_port(void) {
    return test_tcp_port;
}

#define SYSCALL_INT_INSTRUCTION_SIZE 2
#define SYSCALL_PATH_MAX 128
#define SYSCALL_EINTR 4
#define SYSCALL_ECHILD 10
#define SYSCALL_EAGAIN 11

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

static bool syscall_copy_string(char *destination, u64 source, usize capacity) {
    if (destination == NULL || capacity < 2) {
        return false;
    }
    for (usize index = 0; index + 1 < capacity; index++) {
        if (!syscall_buffer_valid(source + index, 1)) {
            return false;
        }
        security_stac();
        char character = *(const char *)(usize)(source + index);
        security_clac();
        destination[index] = character;
        if (character == 0) {
            return true;
        }
    }
    destination[capacity - 1] = 0;
    return true;
}

static u64 syscall_write(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    int descriptor = (int)registers->rdi;
    const char *buffer = (const char *)registers->rsi;
    u64 length = registers->rdx;
    if (descriptor != 1 && descriptor != 2 && (descriptor < 0 || descriptor >= FD_MAX)) {
        return (u64)-1;
    }
    if (length == 0) {
        return 0;
    }
    if (length > 4096 || buffer == NULL || !syscall_buffer_valid((u64)buffer, length)) {
        return (u64)-1;
    }
    security_stac();
    int result = fd_write(&process->fds, descriptor, buffer, length);
    security_clac();
    return result < 0 ? (u64)-1 : (u64)result;
}

static u64 syscall_read(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    int descriptor = (int)registers->rdi;
    void *buffer = (void *)(usize)registers->rsi;
    u64 length = registers->rdx;
    if (length == 0) {
        return 0;
    }
    if (length > 4096 || buffer == NULL || !syscall_buffer_valid((u64)buffer, length)) {
        return (u64)-1;
    }
    security_stac();
    int result = fd_read(&process->fds, descriptor, buffer, length);
    security_clac();
    return result < 0 ? (u64)-1 : (u64)result;
}

static u64 syscall_open(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    char path[SYSCALL_PATH_MAX];
    if (!syscall_copy_string(path, registers->rdi, sizeof(path))) {
        return (u64)-1;
    }
    u8 flags = (u8)registers->rsi;
    if ((flags & (FD_FLAG_READ | FD_FLAG_WRITE)) == 0) {
        flags |= FD_FLAG_READ;
    }
    int result = fd_open_path(&process->fds, vfs_root(), path, flags);
    return result < 0 ? (u64)-1 : (u64)result;
}

static u64 syscall_close(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    int result = fd_close(&process->fds, (int)registers->rdi);
    return result < 0 ? (u64)-1 : 0;
}

static u64 syscall_dup(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    int result = fd_dup(&process->fds, (int)registers->rdi);
    return result < 0 ? (u64)-1 : (u64)result;
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
    u64 timeout_ms = registers->rdx;
    if (status_address != 0 && !syscall_buffer_valid(status_address, sizeof(u64))) {
        return (u64)-1;
    }
    u64 reaped_pid = 0;
    u64 status = 0;
    int result = process_reap_child(process->pid, target, &reaped_pid, &status);
    if (result == PROCESS_ERROR_NONE) {
        process->wait_deadline_ticks = 0;
        process->wait_target = 0;
        if (status_address != 0) {
            security_stac();
            *(u64 *)(usize)status_address = status;
            security_clac();
        }
        return reaped_pid;
    }
    if (result == PROCESS_ERROR_NO_CHILD) {
        return (u64)(-(i64)SYSCALL_ECHILD);
    }
    if (signal_has_deliverable(&process->signals)) {
        process->interrupts++;
        process->wait_deadline_ticks = 0;
        process->wait_target = 0;
        waitq_remove(process->pid);
        return (u64)(-(i64)SYSCALL_EINTR);
    }
    u64 now = timer_ticks();
    if (process->wait_deadline_ticks == 0 && timeout_ms != 0) {
        u64 ticks = (timeout_ms * timer_frequency() + 999) / 1000;
        if (ticks == 0) {
            ticks = 1;
        }
        process->wait_deadline_ticks = now + ticks;
    }
    if (process->wait_deadline_ticks != 0 && now >= process->wait_deadline_ticks) {
        process->wait_deadline_ticks = 0;
        process->wait_target = 0;
        process->wait_timeouts++;
        waitq_remove(process->pid);
        return (u64)(-(i64)SYSCALL_EAGAIN);
    }
    waitq_block(process->pid, target, process->wait_deadline_ticks);
    process->wait_target = target;
    process->state = PROCESS_BLOCKED;
    registers->rip -= SYSCALL_INT_INSTRUCTION_SIZE;
    registers->rax = SYS_WAIT;
    return SYS_WAIT;
}

static u64 syscall_signal(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    if (!process_set_signal(process, registers->rdi, registers->rsi, registers->rdx)) {
        return (u64)-1;
    }
    return 0;
}

static u64 syscall_kill(struct registers *registers) {
    if (registers->rdi == 0 || registers->rsi == 0 || registers->rsi >= SIGNAL_MAX) {
        return (u64)-1;
    }
    if (!process_raise_signal(registers->rdi, registers->rsi)) {
        return (u64)-1;
    }
    return 0;
}

static u64 syscall_sigreturn(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    u64 context = registers->rdi;
    if (context == 0 || !syscall_buffer_valid(context, SIGNAL_FRAME_SIZE)) {
        sched_exit_current(0x91);
    }
    security_stac();
    bool restored = signal_restore(registers, (const void *)(usize)context, SIGNAL_FRAME_SIZE);
    security_clac();
    if (!restored) {
        sched_exit_current(0x92);
    }
    return registers->rax;
}

static u64 syscall_sigprocmask(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL) {
        return (u64)-1;
    }
    u64 mask = registers->rdi;
    u32 operation = (u32)registers->rsi;
    return signal_mask(&process->signals, mask, operation);
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
    security_stac();
    memcpy((void *)(usize)destination, &info, sizeof(info));
    security_clac();
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

static u64 syscall_fb_text(struct registers *registers) {
    struct process *process = syscall_process();
    if (process == NULL || !fb_mode_active()) {
        return (u64)-1;
    }
    u64 x = registers->rdi;
    u64 y = registers->rsi;
    u32 color = (u32)registers->rdx;
    u64 text_address = registers->r10;
    if (x > 0xFFFF || y > 0xFFFF || text_address == 0) {
        return (u64)-1;
    }
    char text[96];
    if (!syscall_copy_string(text, text_address, sizeof(text))) {
        return (u64)-1;
    }
    if (fb_draw_text((u32)x, (u32)y, text, color) != 0) {
        return (u64)-1;
    }
    return 0;
}

static u64 syscall_socket(struct registers *registers) {
    u64 descriptor = (u64)socket_create((u32)registers->rdi, (u32)registers->rsi,
                                        (u32)registers->rdx);
    if ((i64)descriptor < 0) {
        return (u64)(i64)-1;
    }
    return descriptor;
}

static u64 syscall_bind(struct registers *registers) {
    int descriptor = (int)registers->rdi;
    u16 port = (u16)registers->rsi;
    if (socket_bind(descriptor, 0, port) != 0) {
        return (u64)(i64)-1;
    }
    return 0;
}

static u64 syscall_sendto(struct registers *registers) {
    int descriptor = (int)registers->rdi;
    u32 address = (u32)registers->rsi;
    u16 port = (u16)registers->rdx;
    u64 data_address = registers->r10;
    u64 length = registers->r8;
    if (length == 0 || length > 512 || !syscall_buffer_valid(data_address, length)) {
        return (u64)(i64)-1;
    }
    u8 buffer[512];
    security_stac();
    memcpy(buffer, (const void *)(usize)data_address, (usize)length);
    security_clac();
    int sent = socket_sendto(descriptor, address, port, buffer, (u32)length);
    if (sent < 0) {
        return (u64)(i64)-1;
    }
    return (u64)sent;
}

static u64 syscall_recvfrom(struct registers *registers) {
    int descriptor = (int)registers->rdi;
    u64 buffer_address = registers->rsi;
    u64 capacity = registers->rdx;
    u32 timeout_ms = (u32)registers->r10;
    u64 source_ip_address = registers->r8;
    u64 source_port_address = registers->r9;
    if (capacity == 0 || capacity > 512 || !syscall_buffer_valid(buffer_address, capacity)) {
        return (u64)(i64)-1;
    }
    u8 buffer[512];
    u32 source_ip = 0;
    u16 source_port = 0;
    int received = socket_recvfrom(descriptor, buffer, (u32)capacity, timeout_ms,
                                   &source_ip, &source_port);
    if (received < 0) {
        return (u64)(i64)-1;
    }
    security_stac();
    memcpy((void *)(usize)buffer_address, buffer, (usize)received);
    if (source_ip_address != 0 && syscall_buffer_valid(source_ip_address, sizeof(u32))) {
        memcpy((void *)(usize)source_ip_address, &source_ip, sizeof(u32));
    }
    if (source_port_address != 0 && syscall_buffer_valid(source_port_address, sizeof(u16))) {
        memcpy((void *)(usize)source_port_address, &source_port, sizeof(u16));
    }
    security_clac();
    return (u64)received;
}

static u64 syscall_socket_close(struct registers *registers) {
    if (socket_close((int)registers->rdi) != 0) {
        return (u64)(i64)-1;
    }
    return 0;
}

static u64 syscall_connect(struct registers *registers) {
    int result = socket_connect((int)registers->rdi, (u32)registers->rsi, (u16)registers->rdx, 3000);
    if (result != 0) {
        return (u64)(i64)-1;
    }
    return 0;
}

static u64 syscall_accept(struct registers *registers) {
    int accepted = socket_accept((int)registers->rdi, (u32)registers->rsi);
    if (accepted < 0) {
        return (u64)(i64)-1;
    }
    return (u64)accepted;
}

static u64 syscall_recv(struct registers *registers) {
    u64 buffer_address = registers->rsi;
    u64 capacity = registers->rdx;
    u32 timeout_ms = (u32)registers->r10;
    if (capacity == 0 || capacity > 2048 || !syscall_buffer_valid(buffer_address, capacity)) {
        return (u64)(i64)-1;
    }
    u8 buffer[2048];
    int received = socket_recv((int)registers->rdi, buffer, (u32)capacity, timeout_ms);
    if (received < 0) {
        return (u64)(i64)-1;
    }
    security_stac();
    memcpy((void *)(usize)buffer_address, buffer, (usize)received);
    security_clac();
    return (u64)received;
}

static u64 syscall_send(struct registers *registers) {
    u64 buffer_address = registers->rsi;
    u64 length = registers->rdx;
    if (length == 0 || length > 2048 || !syscall_buffer_valid(buffer_address, length)) {
        return (u64)(i64)-1;
    }
    u8 buffer[2048];
    security_stac();
    memcpy(buffer, (const void *)(usize)buffer_address, (usize)length);
    security_clac();
    int sent = socket_send((int)registers->rdi, buffer, (u32)length);
    if (sent < 0) {
        return (u64)(i64)-1;
    }
    return (u64)sent;
}

static u64 syscall_dns(struct registers *registers) {
    char name[128];
    if (!syscall_copy_string(name, registers->rdi, sizeof(name))) {
        return (u64)(i64)-1;
    }
    u32 address = 0;
    if (dns_resolve(name, &address, 2000) != 0) {
        return (u64)(i64)-1;
    }
    if (registers->rsi != 0 && syscall_buffer_valid(registers->rsi, sizeof(u32))) {
        security_stac();
        memcpy((void *)(usize)registers->rsi, &address, sizeof(u32));
        security_clac();
    }
    return (u64)address;
}

static u64 syscall_select(struct registers *registers) {
    u32 read_mask = (u32)registers->rdi;
    u32 write_mask = (u32)registers->rsi;
    u32 timeout_ms = (u32)registers->rdx;
    u64 ready_read_address = registers->r10;
    u64 ready_write_address = registers->r8;
    u32 ready_read = 0;
    u32 ready_write = 0;
    int result = socket_select(read_mask, write_mask, timeout_ms, &ready_read, &ready_write);
    if (result < 0) {
        return (u64)(i64)-1;
    }
    if (ready_read_address != 0 && syscall_buffer_valid(ready_read_address, sizeof(u32))) {
        security_stac();
        memcpy((void *)(usize)ready_read_address, &ready_read, sizeof(u32));
        security_clac();
    }
    if (ready_write_address != 0 && syscall_buffer_valid(ready_write_address, sizeof(u32))) {
        security_stac();
        memcpy((void *)(usize)ready_write_address, &ready_write, sizeof(u32));
        security_clac();
    }
    return (u64)result;
}

static u64 syscall_set_nonblocking(struct registers *registers) {
    if (socket_set_nonblocking((int)registers->rdi, registers->rsi != 0) != 0) {
        return (u64)(i64)-1;
    }
    return 0;
}

static u64 syscall_mouse(struct registers *registers) {
    u64 destination = registers->rdi;
    if (destination == 0 || !syscall_buffer_valid(destination, 16)) {
        return (u64)-1;
    }
    struct mouse_state state;
    mouse_get_state(&state);
    security_stac();
    i32 *target = (i32 *)(usize)destination;
    target[0] = state.x;
    target[1] = state.y;
    ((u32 *)(usize)destination)[2] = state.buttons;
    ((u32 *)(usize)destination)[3] = state.initialized ? 1U : 0U;
    security_clac();
    return 0;
}

void syscall_dispatch(struct registers *registers) {
    switch (registers->rax) {
        case SYS_WRITE:
            registers->rax = syscall_write(registers);
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
        case SYS_OPEN:
            registers->rax = syscall_open(registers);
            break;
        case SYS_CLOSE:
            registers->rax = syscall_close(registers);
            break;
        case SYS_READ:
            registers->rax = syscall_read(registers);
            break;
        case SYS_GETUID:
            registers->rax = syscall_process() != NULL ? syscall_process()->cred.uid : 0;
            break;
        case SYS_GETEUID:
            registers->rax = syscall_process() != NULL ? syscall_process()->cred.euid : 0;
            break;
        case SYS_SIGNAL:
            registers->rax = syscall_signal(registers);
            break;
        case SYS_KILL:
            registers->rax = syscall_kill(registers);
            break;
        case SYS_SIGRETURN:
            registers->rax = syscall_sigreturn(registers);
            break;
        case SYS_SIGPROCMASK:
            registers->rax = syscall_sigprocmask(registers);
            break;
        case SYS_DUP:
            registers->rax = syscall_dup(registers);
            break;
        case SYS_FB_TEXT:
            registers->rax = syscall_fb_text(registers);
            break;
        case SYS_MOUSE:
            registers->rax = syscall_mouse(registers);
            break;
        case SYS_GUI_ENTER:
            gui_request_enter();
            registers->rax = 0;
            break;
        case SYS_GUI_EXIT:
            gui_request_exit();
            registers->rax = 0;
            break;
        case SYS_SOCKET:
            registers->rax = syscall_socket(registers);
            break;
        case SYS_BIND:
            registers->rax = syscall_bind(registers);
            break;
        case SYS_SENDTO:
            registers->rax = syscall_sendto(registers);
            break;
        case SYS_RECVFROM:
            registers->rax = syscall_recvfrom(registers);
            break;
        case SYS_SOCKET_CLOSE:
            registers->rax = syscall_socket_close(registers);
            break;
        case SYS_CONNECT:
            registers->rax = syscall_connect(registers);
            break;
        case SYS_ACCEPT:
            registers->rax = syscall_accept(registers);
            break;
        case SYS_RECV:
            registers->rax = syscall_recv(registers);
            break;
        case SYS_SEND:
            registers->rax = syscall_send(registers);
            break;
        case SYS_DNS:
            registers->rax = syscall_dns(registers);
            break;
        case SYS_GET_TEST_PORT:
            registers->rax = (u64)test_tcp_port;
            break;
        case SYS_SELECT:
            registers->rax = syscall_select(registers);
            break;
        case SYS_SET_NONBLOCKING:
            registers->rax = syscall_set_nonblocking(registers);
            break;
        default:
            registers->rax = (u64)-1;
            break;
    }
}
