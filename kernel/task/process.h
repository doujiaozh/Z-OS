#ifndef Z_OS_PROCESS_H
#define Z_OS_PROCESS_H

#include "types.h"
#include "vmm.h"

#define MAX_PROCESSES 32
#define PROCESS_STACK_TOP VMM_USER_STACK_TOP

#define PROCESS_ERROR_NONE 0
#define PROCESS_ERROR_INVALID -1
#define PROCESS_ERROR_NOT_READY -2
#define PROCESS_ERROR_NO_SPACE -3
#define PROCESS_ERROR_MEMORY -4
#define PROCESS_ERROR_BUSY -5
#define PROCESS_ERROR_NO_CHILD -6

enum process_state {
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_ZOMBIE = 3
};

struct process {
    bool used;
    u64 pid;
    u64 parent_pid;
    enum process_state state;
    char name[24];
    struct address_space space;
    u64 entry;
    u64 user_stack_top;
    u64 exit_status;
    u64 created_ticks;
    u64 exited_ticks;
    struct process *ready_next;
};

void process_init(void);
struct process *process_create(const char *name, u64 parent_pid, const void *image, u64 image_size);
int process_fork(u64 parent_pid, u64 *child_pid);
int process_exec(u64 pid, const void *image, u64 image_size);
int process_exec_current(const void *image, u64 image_size, u64 *entry, u64 *stack);
int process_exit(u64 pid, u64 status);
int process_wait(u64 parent_pid, u64 child_pid, u64 *status);
int process_run(u64 pid, u64 *exit_code);
struct process *process_find(u64 pid);
struct process *process_take_ready(void);
u64 process_current_pid(void);
u64 process_count(void);
u64 process_ready_count(void);
u64 process_high_half_entry(const struct process *process);
u64 process_user_physical(const struct process *process, u64 virtual_address);
int process_switch_space(struct process *process);

#endif
