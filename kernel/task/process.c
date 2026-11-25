#include "process.h"
#include "elf.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "timer.h"
#include "vmm.h"
#include "waitq.h"

static struct process process_table[MAX_PROCESSES];
static struct process *ready_head = NULL;
static struct process *ready_tail = NULL;
static struct process *current_process = NULL;
static u64 next_process_pid = 1;
static u32 ready_count = 0;

static struct process *allocate_process(const char *name, u64 parent_pid) {
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        if (!process_table[index].used) {
            memset(&process_table[index], 0, sizeof(struct process));
            process_table[index].used = true;
            process_table[index].pid = next_process_pid++;
            process_table[index].parent_pid = parent_pid;
            process_table[index].state = PROCESS_UNUSED;
            process_table[index].created_ticks = timer_ticks();
            process_table[index].quantum = PROCESS_QUANTUM_TICKS;
            fd_table_init(&process_table[index].fds);
            cred_init_root(&process_table[index].cred);
            signal_init(&process_table[index].signals);
            strncpy(process_table[index].name, name, sizeof(process_table[index].name) - 1);
            process_table[index].name[sizeof(process_table[index].name) - 1] = 0;
            return &process_table[index];
        }
    }
    return NULL;
}

static void release_process(struct process *process) {
    if (process == NULL) {
        return;
    }
    if (process->kernel_stack_base != 0) {
        kfree((void *)(usize)process->kernel_stack_base);
        process->kernel_stack_base = 0;
    }
    memset(process, 0, sizeof(struct process));
}

static void drain_ready_queue(void) {
    ready_head = NULL;
    ready_tail = NULL;
    ready_count = 0;
}

void process_enqueue_ready(struct process *process) {
    if (process == NULL || process->state == PROCESS_READY) {
        return;
    }
    u64 flags = irq_save();
    process->ready_next = NULL;
    process->state = PROCESS_READY;
    if (ready_tail == NULL) {
        ready_head = process;
        ready_tail = process;
    } else {
        ready_tail->ready_next = process;
        ready_tail = process;
    }
    ready_count++;
    irq_restore(flags);
}

void process_dequeue_ready(struct process *process) {
    if (process == NULL) {
        return;
    }
    u64 flags = irq_save();
    struct process **cursor = &ready_head;
    while (*cursor != NULL) {
        if (*cursor == process) {
            *cursor = process->ready_next;
            if (ready_tail == process) {
                ready_tail = ready_head;
                while (ready_tail != NULL && ready_tail->ready_next != NULL) {
                    ready_tail = ready_tail->ready_next;
                }
            }
            process->ready_next = NULL;
            if (ready_count > 0) {
                ready_count--;
            }
            break;
        }
        cursor = &(*cursor)->ready_next;
    }
    irq_restore(flags);
}

struct process *process_pick_ready(void) {
    u64 flags = irq_save();
    struct process *process = ready_head;
    if (process != NULL) {
        ready_head = process->ready_next;
        if (ready_head == NULL) {
            ready_tail = NULL;
        }
        process->ready_next = NULL;
        if (ready_count > 0) {
            ready_count--;
        }
    }
    irq_restore(flags);
    return process;
}

static int process_load_user_stack(struct process *process) {
    u64 stack_frame = pmm_alloc_frame();
    if (stack_frame == 0) {
        return PROCESS_ERROR_MEMORY;
    }
    if (vmm_map_user_page(&process->space, VMM_USER_STACK_BASE, stack_frame,
                          PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
        pmm_free_frame(stack_frame);
        return PROCESS_ERROR_MEMORY;
    }
    process->user_stack_top = PROCESS_STACK_TOP;
    return PROCESS_ERROR_NONE;
}

static int process_allocate_kernel_stack(struct process *process) {
    process->kernel_stack_base = (u64)kmalloc(PROCESS_KERNEL_STACK_SIZE);
    if (process->kernel_stack_base == 0) {
        return PROCESS_ERROR_MEMORY;
    }
    memset((void *)(usize)process->kernel_stack_base, 0, PROCESS_KERNEL_STACK_SIZE);
    process->kernel_stack_top = (process->kernel_stack_base + PROCESS_KERNEL_STACK_SIZE) & ~0xFULL;
    return PROCESS_ERROR_NONE;
}

static struct registers *process_frame_slot(struct process *process) {
    return (struct registers *)(process->kernel_stack_top - sizeof(struct registers));
}

static void process_reset_frame(struct registers *frame, u64 entry, u64 user_stack) {
    memset(frame, 0, sizeof(struct registers));
    frame->rip = entry;
    frame->cs = GDT_USER_CODE | 3;
    frame->rflags = 0x202;
    frame->rsp = user_stack;
    frame->ss = GDT_USER_DATA | 3;
    frame->rax = 0;
}

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    drain_ready_queue();
    waitq_init();
    current_process = NULL;
    next_process_pid = 1;
    kprintf("Process: %u slots, kernel stack %u KiB, quantum %u ticks\n",
            MAX_PROCESSES, PROCESS_KERNEL_STACK_SIZE / 1024, PROCESS_QUANTUM_TICKS);
}

struct process *process_find(u64 pid) {
    if (pid == 0) {
        return NULL;
    }
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        if (process_table[index].used && process_table[index].pid == pid) {
            return &process_table[index];
        }
    }
    return NULL;
}

struct process *process_current(void) {
    return current_process;
}

void process_set_current(struct process *process) {
    current_process = process;
}

struct process *process_create(const char *name, u64 parent_pid, const void *image, u64 image_size) {
    if (image == NULL || image_size == 0) {
        return NULL;
    }
    u64 flags = irq_save();
    struct process *process = allocate_process(name, parent_pid);
    irq_restore(flags);
    if (process == NULL) {
        return NULL;
    }
    if (vmm_create_address_space(&process->space) != 0) {
        release_process(process);
        return NULL;
    }
    u64 entry = 0;
    if (elf_load_into(&process->space, image, image_size, &entry) != ELF_OK) {
        vmm_destroy_address_space(&process->space);
        release_process(process);
        return NULL;
    }
    if (process_load_user_stack(process) != PROCESS_ERROR_NONE) {
        vmm_destroy_address_space(&process->space);
        release_process(process);
        return NULL;
    }
    if (process_allocate_kernel_stack(process) != PROCESS_ERROR_NONE) {
        vmm_destroy_address_space(&process->space);
        release_process(process);
        return NULL;
    }
    process->entry = entry;
    process_reset_frame(process_frame_slot(process), entry, process->user_stack_top);
    process->frame_rsp = process->kernel_stack_top - sizeof(struct registers);
    process->quantum = PROCESS_QUANTUM_TICKS;
    process_enqueue_ready(process);
    return process;
}

int process_fork_frame(u64 parent_pid, const struct registers *frame, u64 *child_pid) {
    struct process *parent = process_find(parent_pid);
    if (parent == NULL || frame == NULL || child_pid == NULL) {
        return PROCESS_ERROR_INVALID;
    }
    char child_name[24];
    strncpy(child_name, parent->name, sizeof(child_name) - 1);
    child_name[sizeof(child_name) - 1] = 0;
    u64 flags = irq_save();
    struct process *child = allocate_process(child_name, parent_pid);
    irq_restore(flags);
    if (child == NULL) {
        return PROCESS_ERROR_NO_SPACE;
    }
    if (vmm_clone_address_space(&parent->space, &child->space) != 0) {
        release_process(child);
        return PROCESS_ERROR_MEMORY;
    }
    if (process_allocate_kernel_stack(child) != PROCESS_ERROR_NONE) {
        vmm_destroy_address_space(&child->space);
        release_process(child);
        return PROCESS_ERROR_MEMORY;
    }
    child->entry = parent->entry;
    child->user_stack_top = parent->user_stack_top;
    fd_copy(&child->fds, &parent->fds);
    cred_inherit(&parent->cred, &child->cred);
    child->signals.blocked = parent->signals.blocked;
    child->signals.trampoline = parent->signals.trampoline;
    child->signals.handler[0] = 0;
    memcpy(child->signals.handler, parent->signals.handler, sizeof(parent->signals.handler));
    child->signals.installed = parent->signals.installed;
    struct registers *child_frame = process_frame_slot(child);
    memcpy(child_frame, frame, sizeof(struct registers));
    child_frame->rax = 0;
    child->frame_rsp = child->kernel_stack_top - sizeof(struct registers);
    child->quantum = PROCESS_QUANTUM_TICKS;
    process_enqueue_ready(child);
    *child_pid = child->pid;
    return PROCESS_ERROR_NONE;
}

int process_exec_current(const void *image, u64 image_size, struct registers *frame) {
    struct process *process = current_process;
    if (process == NULL || image == NULL || frame == NULL) {
        return PROCESS_ERROR_INVALID;
    }
    struct address_space replacement;
    if (vmm_create_address_space(&replacement) != 0) {
        return PROCESS_ERROR_MEMORY;
    }
    u64 entry = 0;
    if (elf_load_into(&replacement, image, image_size, &entry) != ELF_OK) {
        vmm_destroy_address_space(&replacement);
        return PROCESS_ERROR_MEMORY;
    }
    u64 stack_frame = pmm_alloc_frame();
    if (stack_frame == 0) {
        vmm_destroy_address_space(&replacement);
        return PROCESS_ERROR_MEMORY;
    }
    if (vmm_map_user_page(&replacement, VMM_USER_STACK_BASE, stack_frame,
                          PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
        pmm_free_frame(stack_frame);
        vmm_destroy_address_space(&replacement);
        return PROCESS_ERROR_MEMORY;
    }
    struct address_space previous = process->space;
    if (vmm_switch_address_space(&replacement) != 0) {
        vmm_destroy_address_space(&replacement);
        return PROCESS_ERROR_MEMORY;
    }
    process->space = replacement;
    process->entry = entry;
    process->user_stack_top = PROCESS_STACK_TOP;
    vmm_destroy_address_space(&previous);
    process_reset_frame(frame, entry, process->user_stack_top);
    return PROCESS_ERROR_NONE;
}

void process_mark_zombie(struct process *process, u64 status) {
    if (process == NULL || process->state == PROCESS_ZOMBIE) {
        return;
    }
    if (process->state == PROCESS_READY) {
        process_dequeue_ready(process);
    }
    process->state = PROCESS_ZOMBIE;
    process->exit_status = status;
    process->exited_ticks = timer_ticks();
    waitq_remove(process->pid);
}

void process_wake_parent(struct process *child) {
    if (child == NULL) {
        return;
    }
    struct process *parent = process_find(child->parent_pid);
    if (parent == NULL) {
        return;
    }
    if (parent->state != PROCESS_BLOCKED) {
        return;
    }
    if (parent->wait_target != 0 && parent->wait_target != child->pid) {
        return;
    }
    parent->wait_target = 0;
    waitq_remove(parent->pid);
    process_enqueue_ready(parent);
}

bool process_has_pending_signal(struct process *process) {
    return process != NULL && signal_has_deliverable(&process->signals);
}

bool process_deliver_signal(struct process *process, struct registers *frame) {
    return signal_deliver(process, frame) > 0;
}

bool process_set_signal(struct process *process, u64 signum, u64 handler, u64 trampoline) {
    if (process == NULL) {
        return false;
    }
    if (signum == 0 || signum >= SIGNAL_MAX || signum == SIGNAL_SIGKILL || signum == SIGNAL_SIGSTOP) {
        return false;
    }
    signal_install(&process->signals, signum, handler, trampoline);
    return true;
}

bool process_raise_signal(u64 pid, u64 signum) {
    struct process *target = process_find(pid);
    if (target == NULL || target->state == PROCESS_ZOMBIE || target->state == PROCESS_UNUSED) {
        return false;
    }
    if (!signal_raise(&target->signals, signum)) {
        return false;
    }
    if (target->state == PROCESS_BLOCKED && !signal_is_blocked(&target->signals, signum)) {
        target->wait_target = 0;
        waitq_remove(target->pid);
        process_enqueue_ready(target);
    }
    return true;
}

static void reclaim_process(struct process *process) {
    vmm_destroy_address_space(&process->space);
    release_process(process);
}

int process_reap(u64 pid) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state != PROCESS_ZOMBIE || process == current_process) {
        return PROCESS_ERROR_INVALID;
    }
    u64 status = process->exit_status;
    reclaim_process(process);
    return (int)status;
}

int process_reap_child(u64 parent_pid, u64 target_pid, u64 *reaped_pid, u64 *status) {
    u64 flags = irq_save();
    struct process *found = NULL;
    bool alive_child = false;
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        struct process *candidate = &process_table[index];
        if (!candidate->used || candidate->parent_pid != parent_pid) {
            continue;
        }
        if (target_pid != 0 && candidate->pid != target_pid) {
            continue;
        }
        if (candidate->state == PROCESS_ZOMBIE) {
            found = candidate;
            break;
        }
        alive_child = true;
    }
    if (found == NULL) {
        irq_restore(flags);
        return alive_child ? PROCESS_ERROR_NEED_BLOCK : PROCESS_ERROR_NO_CHILD;
    }
    u64 pid = found->pid;
    u64 exit_status = found->exit_status;
    reclaim_process(found);
    waitq_remove(parent_pid);
    irq_restore(flags);
    if (reaped_pid != NULL) {
        *reaped_pid = pid;
    }
    if (status != NULL) {
        *status = exit_status;
    }
    return PROCESS_ERROR_NONE;
}

u32 process_reap_zombies(void) {
    u32 reaped = 0;
    u64 flags = irq_save();
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        struct process *candidate = &process_table[index];
        if (!candidate->used || candidate->state != PROCESS_ZOMBIE || candidate == current_process) {
            continue;
        }
        struct process *parent = process_find(candidate->parent_pid);
        if (parent != NULL && parent->state != PROCESS_ZOMBIE && parent != candidate) {
            continue;
        }
        reclaim_process(candidate);
        reaped++;
    }
    irq_restore(flags);
    return reaped;
}

int process_kill(u64 pid, u64 status) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state == PROCESS_ZOMBIE || process == current_process) {
        return PROCESS_ERROR_INVALID;
    }
    process_mark_zombie(process, status);
    process_wake_parent(process);
    return PROCESS_ERROR_NONE;
}

u64 process_current_pid(void) {
    return current_process != NULL ? current_process->pid : 0;
}

u64 process_count(void) {
    u64 count = 0;
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        if (process_table[index].used) {
            count++;
        }
    }
    return count;
}

u64 process_ready_count(void) {
    return ready_count;
}

u64 process_blocked_count(void) {
    u64 count = 0;
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        if (process_table[index].used && process_table[index].state == PROCESS_BLOCKED) {
            count++;
        }
    }
    return count;
}

u64 process_high_half_entry(const struct process *process) {
    if (process == NULL || !process->space.ready) {
        return 0;
    }
    u64 *pml4 = (u64 *)process->space.pml4_phys;
    return pml4[511];
}

u64 process_user_physical(const struct process *process, u64 virtual_address) {
    if (process == NULL) {
        return 0;
    }
    return vmm_user_physical(&process->space, virtual_address);
}

u32 process_snapshot(struct process *buffer, u32 capacity) {
    u32 written = 0;
    for (u32 index = 0; index < MAX_PROCESSES && written < capacity; index++) {
        if (!process_table[index].used) {
            continue;
        }
        buffer[written++] = process_table[index];
    }
    return written;
}

const char *process_state_name(enum process_state state) {
    switch (state) {
        case PROCESS_READY:
            return "ready";
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_BLOCKED:
            return "blocked";
        case PROCESS_ZOMBIE:
            return "zombie";
        default:
            return "unused";
    }
}
