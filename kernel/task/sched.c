#include "sched.h"
#include "gdt.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"
#include "process.h"
#include "signal.h"
#include "string.h"
#include "vmm.h"

struct sched_mark {
    u64 pid;
    u64 marker;
};

extern void sched_enter_user(u64 *dispatcher_rsp, u64 frame_rsp);
extern void sched_resume_user_frame(u64 frame_rsp) __attribute__((noreturn));
extern void sched_abandon_to_dispatcher(u64 dispatcher_rsp) __attribute__((noreturn));

static bool batch_active = false;
static u64 dispatcher_rsp = 0;
static u64 batch_pids[SCHED_MAX_BATCH];
static u32 batch_count = 0;
static int batch_error = SCHED_ERROR_NONE;
static u64 preemption_count = 0;
static struct sched_mark marks[SCHED_MARK_SLOTS];
static u32 mark_count = 0;
static u64 marker_totals[SCHED_MARK_KINDS];

void sched_process_init(void) {
    batch_active = false;
    dispatcher_rsp = 0;
    batch_count = 0;
    batch_error = SCHED_ERROR_NONE;
    preemption_count = 0;
    sched_mark_reset();
    kprintf("ProcSched: preemptive round-robin, quantum %u ticks\n", PROCESS_QUANTUM_TICKS);
}

bool sched_user_active(void) {
    return batch_active;
}

u64 sched_preemption_count(void) {
    return preemption_count;
}

u64 sched_quantum_ticks(void) {
    return PROCESS_QUANTUM_TICKS;
}

u64 sched_switch_count_of(u64 pid) {
    struct process *process = process_find(pid);
    return process != NULL ? process->context_switches : 0;
}

void sched_user_tick(void) {
    if (!batch_active) {
        return;
    }
    struct process *current = process_current();
    if (current == NULL) {
        return;
    }
    current->cpu_ticks++;
    if (current->state == PROCESS_RUNNING && current->quantum > 0) {
        current->quantum--;
    }
}

static void activate_process(struct process *process) {
    process->state = PROCESS_RUNNING;
    process->quantum = PROCESS_QUANTUM_TICKS;
    process->context_switches++;
    process_set_current(process);
    write_cr3(process->space.pml4_phys);
    tss_set_rsp0(process->kernel_stack_top);
    if (process->kpti_ready && process->kpti.built) {
        kpti_blend_slots(process->space.pml4_phys, process->kpti.cr3);
    } else {
        kpti_clear_return_slot();
    }
}

static bool batch_has_blocked_process(void) {
    for (u32 index = 0; index < batch_count; index++) {
        struct process *process = process_find(batch_pids[index]);
        if (process != NULL && process->state == PROCESS_BLOCKED) {
            return true;
        }
    }
    return false;
}

static void end_batch(void) {
    batch_error = batch_has_blocked_process() ? SCHED_ERROR_DEADLOCK : SCHED_ERROR_NONE;
    sched_abandon_to_dispatcher(dispatcher_rsp);
}

static void terminate_all_batch_processes(u64 status) {
    for (u32 index = 0; index < batch_count; index++) {
        struct process *process = process_find(batch_pids[index]);
        if (process != NULL && process->state != PROCESS_ZOMBIE) {
            process_mark_zombie(process, status);
        }
    }
}

void sched_exit_current(u64 status) {
    struct process *current = process_current();
    process_mark_zombie(current, status);
    process_wake_parent(current);
    struct process *next = process_pick_ready();
    if (next == NULL) {
        end_batch();
    }
    activate_process(next);
    sched_resume_user_frame(next->frame_rsp);
}

void sched_terminate_current(u64 status) {
    sched_exit_current(status);
}

u64 sched_interrupt_return(u64 frame_rsp) {
    if (!batch_active) {
        return 0;
    }
    struct process *current = process_current();
    if (current == NULL || frame_rsp == 0) {
        return 0;
    }
    struct registers *frame = (struct registers *)frame_rsp;
    if ((frame->cs & 3) != 3) {
        return 0;
    }
    current->frame_rsp = frame_rsp;
    if (signal_has_deliverable(&current->signals)) {
        int action = signal_deliver(current, frame);
        if (action < 0) {
            sched_exit_current(0x80U | (u64)(frame->vector & 0x1F));
        }
    }
    if (current->state == PROCESS_BLOCKED) {
        struct process *next = process_pick_ready();
        if (next == NULL) {
            end_batch();
        }
        activate_process(next);
        preemption_count++;
        return next->frame_rsp;
    }
    if (current->state != PROCESS_RUNNING || current->quantum > 0) {
        return 0;
    }
    current->quantum = PROCESS_QUANTUM_TICKS;
    process_enqueue_ready(current);
    struct process *next = process_pick_ready();
    if (next == NULL) {
        process_set_current(current);
        current->state = PROCESS_RUNNING;
        return 0;
    }
    if (next == current) {
        current->state = PROCESS_RUNNING;
        return 0;
    }
    activate_process(next);
    preemption_count++;
    return next->frame_rsp;
}

int sched_run_batch(const u64 *pids, u32 count, u32 *completed) {
    if (pids == NULL || count == 0 || count > SCHED_MAX_BATCH) {
        return SCHED_ERROR_INVALID;
    }
    if (batch_active) {
        return SCHED_ERROR_BUSY;
    }
    batch_active = true;
    batch_count = count;
    batch_error = SCHED_ERROR_NONE;
    for (u32 index = 0; index < count; index++) {
        batch_pids[index] = pids[index];
        struct process *process = process_find(pids[index]);
        if (process == NULL || process->state != PROCESS_READY) {
            batch_active = false;
            batch_count = 0;
            return SCHED_ERROR_INVALID;
        }
    }
    interrupts_disable();
    struct process *next = process_pick_ready();
    while (next != NULL) {
        activate_process(next);
        sched_enter_user(&dispatcher_rsp, next->frame_rsp);
        process_set_current(NULL);
        write_cr3(vmm_boot_pml4());
        next = process_pick_ready();
    }
    batch_active = false;
    if (batch_error != SCHED_ERROR_NONE) {
        terminate_all_batch_processes(0xDEAD0000ULL | (u64)(-batch_error));
    }
    interrupts_enable();
    u32 done = 0;
    for (u32 index = 0; index < count; index++) {
        struct process *process = process_find(pids[index]);
        if (process == NULL || process->state == PROCESS_ZOMBIE) {
            done++;
        }
    }
    batch_count = 0;
    if (completed != NULL) {
        *completed = done;
    }
    return batch_error;
}

void sched_record_mark(u64 marker) {
    u64 pid = process_current_pid();
    if (mark_count < SCHED_MARK_SLOTS) {
        marks[mark_count].pid = pid;
        marks[mark_count].marker = marker;
        mark_count++;
    }
    if (marker < SCHED_MARK_KINDS) {
        marker_totals[marker]++;
    }
}

u32 sched_mark_count(void) {
    return mark_count;
}

bool sched_mark_get(u32 index, u64 *pid, u64 *marker) {
    if (index >= mark_count || pid == NULL || marker == NULL) {
        return false;
    }
    *pid = marks[index].pid;
    *marker = marks[index].marker;
    return true;
}

u64 sched_marker_total(u64 marker) {
    if (marker >= SCHED_MARK_KINDS) {
        return 0;
    }
    return marker_totals[marker];
}

void sched_mark_reset(void) {
    memset(marks, 0, sizeof(marks));
    memset(marker_totals, 0, sizeof(marker_totals));
    mark_count = 0;
}
