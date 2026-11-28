#ifndef Z_OS_PROCESS_H
#define Z_OS_PROCESS_H

#include "cred.h"
#include "fd.h"
#include "kpti.h"
#include "signal.h"
#include "task.h"
#include "types.h"
#include "vmm.h"

#define MAX_PROCESSES 32
#define PROCESS_STACK_TOP VMM_USER_STACK_TOP
#define PROCESS_KERNEL_STACK_SIZE (16 * 1024)
#define PROCESS_QUANTUM_TICKS 2

#define PROCESS_ERROR_NONE 0
#define PROCESS_ERROR_INVALID -1
#define PROCESS_ERROR_NOT_READY -2
#define PROCESS_ERROR_NO_SPACE -3
#define PROCESS_ERROR_MEMORY -4
#define PROCESS_ERROR_BUSY -5
#define PROCESS_ERROR_NO_CHILD -6
#define PROCESS_ERROR_NEED_BLOCK 1

enum process_state {
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_ZOMBIE = 4
};

struct process {
    struct task_base task;
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
    u64 kernel_stack_base;
    u64 kernel_stack_top;
    u64 frame_rsp;
    u64 cpu_ticks;
    u64 context_switches;
    u64 quantum;
    u64 wait_target;
    u64 wait_deadline_ticks;
    u64 wait_timeouts;
    u64 interrupts;
    struct fd_table fds;
    struct credentials cred;
    struct signal_state signals;
    struct kpti_view kpti;
    bool kpti_ready;
    struct process *ready_next;
};

struct registers;

void process_init(void);
struct process *process_create(const char *name, u64 parent_pid, const void *image, u64 image_size);
int process_fork_frame(u64 parent_pid, const struct registers *frame, u64 *child_pid);
int process_exec_current(const void *image, u64 image_size, struct registers *frame);
void process_mark_zombie(struct process *process, u64 status);
bool process_has_pending_signal(struct process *process);
bool process_deliver_signal(struct process *process, struct registers *frame);
bool process_set_signal(struct process *process, u64 signum, u64 handler, u64 trampoline);
bool process_raise_signal(u64 pid, u64 signum);
void process_wake_parent(struct process *child);
int process_kill(u64 pid, u64 status);
int process_reap(u64 pid);
int process_reap_child(u64 parent_pid, u64 target_pid, u64 *reaped_pid, u64 *status);
u32 process_reap_zombies(void);
struct process *process_find(u64 pid);
struct process *process_current(void);
void process_set_current(struct process *process);
void process_enqueue_ready(struct process *process);
void process_dequeue_ready(struct process *process);
struct process *process_pick_ready(void);
u64 process_current_pid(void);
u64 process_count(void);
u64 process_ready_count(void);
u64 process_blocked_count(void);
u64 process_high_half_entry(const struct process *process);
u64 process_user_physical(const struct process *process, u64 virtual_address);
u32 process_snapshot(struct process *buffer, u32 capacity);
const char *process_state_name(enum process_state state);

#endif
