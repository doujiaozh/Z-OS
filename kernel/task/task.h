#ifndef Z_OS_TASK_H
#define Z_OS_TASK_H

#include "types.h"

#define TASK_KIND_NONE 0
#define TASK_KIND_THREAD 1
#define TASK_KIND_PROCESS 2
#define TASK_STATE_UNUSED 0
#define TASK_STATE_READY 1
#define TASK_STATE_RUNNING 2
#define TASK_STATE_BLOCKED 3
#define TASK_STATE_ZOMBIE 4

struct task_base {
    u32 kind;
    u32 identifier;
    u32 state;
    u32 quantum;
    u64 wake_tick;
};

void task_model_init(void);
void task_base_init(struct task_base *task, u32 kind, u32 identifier);
void task_base_set_state(struct task_base *task, u32 state);
void task_base_set_quantum(struct task_base *task, u32 quantum);
const char *task_kind_name(u32 kind);
const char *task_state_name(u32 state);
u32 task_base_threads(void);
u32 task_base_processes(void);
u32 task_base_total(void);
u32 task_base_ready(void);
u32 task_base_running(void);
bool task_base_is_thread(const struct task_base *task);
bool task_base_is_process(const struct task_base *task);

#endif
