#include "waitq.h"
#include "lib.h"
#include "process.h"
#include "string.h"

struct waitq_entry {
    bool used;
    u64 waiting_pid;
    u64 target_pid;
    u64 deadline_ticks;
};

static struct waitq_entry entries[WAITQ_MAX];

void waitq_init(void) {
    memset(entries, 0, sizeof(entries));
}

static struct waitq_entry *find_entry(u64 waiting_pid) {
    for (u32 index = 0; index < WAITQ_MAX; index++) {
        if (entries[index].used && entries[index].waiting_pid == waiting_pid) {
            return &entries[index];
        }
    }
    return NULL;
}

static void wake_process(u64 pid) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state != PROCESS_BLOCKED) {
        return;
    }
    process_enqueue_ready(process);
}

int waitq_block(u64 waiting_pid, u64 target_pid, u64 deadline_ticks) {
    if (waiting_pid == 0) {
        return -1;
    }
    struct waitq_entry *entry = find_entry(waiting_pid);
    if (entry == NULL) {
        for (u32 index = 0; index < WAITQ_MAX; index++) {
            if (!entries[index].used) {
                entry = &entries[index];
                break;
            }
        }
    }
    if (entry == NULL) {
        return -1;
    }
    entry->used = true;
    entry->waiting_pid = waiting_pid;
    entry->target_pid = target_pid;
    entry->deadline_ticks = deadline_ticks;
    return 0;
}

void waitq_remove(u64 waiting_pid) {
    struct waitq_entry *entry = find_entry(waiting_pid);
    if (entry != NULL) {
        memset(entry, 0, sizeof(struct waitq_entry));
    }
}

u32 waitq_wake_target(u64 child_pid) {
    u32 woken = 0;
    for (u32 index = 0; index < WAITQ_MAX; index++) {
        struct waitq_entry *entry = &entries[index];
        if (!entry->used) {
            continue;
        }
        if (entry->target_pid != 0 && entry->target_pid != child_pid) {
            continue;
        }
        wake_process(entry->waiting_pid);
        memset(entry, 0, sizeof(struct waitq_entry));
        woken++;
    }
    return woken;
}

u32 waitq_expire(u64 now_ticks) {
    u32 woken = 0;
    for (u32 index = 0; index < WAITQ_MAX; index++) {
        struct waitq_entry *entry = &entries[index];
        if (!entry->used || entry->deadline_ticks == 0) {
            continue;
        }
        if (now_ticks < entry->deadline_ticks) {
            continue;
        }
        wake_process(entry->waiting_pid);
        memset(entry, 0, sizeof(struct waitq_entry));
        woken++;
    }
    return woken;
}

u32 waitq_count(void) {
    u32 count = 0;
    for (u32 index = 0; index < WAITQ_MAX; index++) {
        if (entries[index].used) {
            count++;
        }
    }
    return count;
}

u64 waitq_deadline(u64 waiting_pid) {
    struct waitq_entry *entry = find_entry(waiting_pid);
    return entry != NULL ? entry->deadline_ticks : 0;
}
