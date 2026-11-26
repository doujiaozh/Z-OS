#include "task.h"
#include "lib.h"
#include "string.h"

static u32 thread_tasks = 0;
static u32 process_tasks = 0;

void task_model_init(void) {
    thread_tasks = 0;
    process_tasks = 0;
}

void task_base_init(struct task_base *task, u32 kind, u32 identifier) {
    if (task == NULL) {
        return;
    }
    memset(task, 0, sizeof(struct task_base));
    task->kind = kind;
    task->identifier = identifier;
    task->state = TASK_STATE_READY;
    task->quantum = 1;
    if (kind == TASK_KIND_THREAD) {
        thread_tasks++;
    } else if (kind == TASK_KIND_PROCESS) {
        process_tasks++;
    }
}

void task_base_set_state(struct task_base *task, u32 state) {
    if (task != NULL) {
        task->state = state;
    }
}

void task_base_set_quantum(struct task_base *task, u32 quantum) {
    if (task != NULL) {
        task->quantum = quantum;
    }
}

const char *task_kind_name(u32 kind) {
    if (kind == TASK_KIND_THREAD) {
        return "thread";
    }
    if (kind == TASK_KIND_PROCESS) {
        return "process";
    }
    return "none";
}

const char *task_state_name(u32 state) {
    if (state == TASK_STATE_READY) {
        return "ready";
    }
    if (state == TASK_STATE_RUNNING) {
        return "running";
    }
    if (state == TASK_STATE_BLOCKED) {
        return "blocked";
    }
    if (state == TASK_STATE_ZOMBIE) {
        return "zombie";
    }
    return "unused";
}

u32 task_base_threads(void) {
    return thread_tasks;
}

u32 task_base_processes(void) {
    return process_tasks;
}

u32 task_base_total(void) {
    return thread_tasks + process_tasks;
}

u32 task_base_ready(void) {
    return thread_tasks + process_tasks;
}

u32 task_base_running(void) {
    return (thread_tasks > 0 || process_tasks > 0) ? 1 : 0;
}

bool task_base_is_thread(const struct task_base *task) {
    return task != NULL && task->kind == TASK_KIND_THREAD;
}

bool task_base_is_process(const struct task_base *task) {
    return task != NULL && task->kind == TASK_KIND_PROCESS;
}
