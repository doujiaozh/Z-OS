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

#endif
