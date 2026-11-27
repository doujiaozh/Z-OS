#ifndef Z_OS_SYSCALL_H
#define Z_OS_SYSCALL_H

#include "types.h"

struct registers;
void syscall_dispatch(struct registers *registers);
void gui_request_enter(void);
void gui_request_exit(void);
bool gui_enter_requested(void);
bool gui_exit_requested(void);
void gui_clear_requests(void);
struct pollfd_user {
    i32 fd;
    u16 events;
    u16 revents;
};

u64 syscall_poll_register(struct pollfd_user *entries, u32 count, u32 timeout_ms);
void syscall_set_test_port(u32 port);
u32 syscall_get_test_port(void);

#endif
