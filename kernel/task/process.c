#include "process.h"
#include "elf.h"
#include "heap.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vmm.h"

static struct process process_table[MAX_PROCESSES];
static struct process *ready_head = NULL;
static struct process *ready_tail = NULL;
static u64 next_process_pid = 1;
static u64 current_process_pid = 0;
static u32 ready_count = 0;

static struct process *process_allocate_locked(const char *name, u64 parent_pid) {
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        if (!process_table[index].used) {
            memset(&process_table[index], 0, sizeof(struct process));
            process_table[index].used = true;
            process_table[index].pid = next_process_pid++;
            process_table[index].parent_pid = parent_pid;
            process_table[index].state = PROCESS_UNUSED;
            process_table[index].created_ticks = timer_ticks();
            strncpy(process_table[index].name, name, sizeof(process_table[index].name) - 1);
            process_table[index].name[sizeof(process_table[index].name) - 1] = 0;
            return &process_table[index];
        }
    }
    return NULL;
}

static void process_release(struct process *process) {
    if (process == NULL) {
        return;
    }
    memset(process, 0, sizeof(struct process));
}

static void ready_enqueue_locked(struct process *process) {
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
}

static void ready_remove_locked(struct process *process) {
    struct process **cursor = &ready_head;
    while (*cursor != NULL) {
        if (*cursor == process) {
            *cursor = process->ready_next;
            if (ready_tail == process) {
                ready_tail = NULL;
                struct process *tail_cursor = ready_head;
                while (tail_cursor != NULL && tail_cursor->ready_next != NULL) {
                    tail_cursor = tail_cursor->ready_next;
                }
                ready_tail = tail_cursor;
            }
            process->ready_next = NULL;
            if (ready_count > 0) {
                ready_count--;
            }
            return;
        }
        cursor = &(*cursor)->ready_next;
    }
}

static int process_load_stack(struct process *process) {
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

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    ready_head = NULL;
    ready_tail = NULL;
    next_process_pid = 1;
    current_process_pid = 0;
    ready_count = 0;
    kprintf("Process: table ready for %u processes\n", MAX_PROCESSES);
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

struct process *process_create(const char *name, u64 parent_pid, const void *image, u64 image_size) {
    if (image == NULL || image_size == 0) {
        return NULL;
    }
    u64 flags = irq_save();
    struct process *process = process_allocate_locked(name, parent_pid);
    irq_restore(flags);
    if (process == NULL) {
        return NULL;
    }
    if (vmm_create_address_space(&process->space) != 0) {
        u64 release_flags = irq_save();
        process_release(process);
        irq_restore(release_flags);
        return NULL;
    }
    u64 entry = 0;
    if (elf_load_into(&process->space, image, image_size, &entry) != ELF_OK) {
        vmm_destroy_address_space(&process->space);
        u64 release_flags = irq_save();
        process_release(process);
        irq_restore(release_flags);
        return NULL;
    }
    if (process_load_stack(process) != PROCESS_ERROR_NONE) {
        vmm_destroy_address_space(&process->space);
        u64 release_flags = irq_save();
        process_release(process);
        irq_restore(release_flags);
        return NULL;
    }
    process->entry = entry;
    process->state = PROCESS_READY;
    u64 enqueue_flags = irq_save();
    ready_enqueue_locked(process);
    irq_restore(enqueue_flags);
    return process;
}

int process_fork(u64 parent_pid, u64 *child_pid) {
    struct process *parent = process_find(parent_pid);
    if (parent == NULL || parent->state == PROCESS_UNUSED || child_pid == NULL) {
        return PROCESS_ERROR_INVALID;
    }
    u64 flags = irq_save();
    char child_name[24];
    strncpy(child_name, parent->name, sizeof(child_name) - 1);
    child_name[sizeof(child_name) - 1] = 0;
    struct process *child = process_allocate_locked(child_name, parent_pid);
    irq_restore(flags);
    if (child == NULL) {
        return PROCESS_ERROR_NO_SPACE;
    }
    if (vmm_clone_address_space(&parent->space, &child->space) != 0) {
        u64 release_flags = irq_save();
        process_release(child);
        irq_restore(release_flags);
        return PROCESS_ERROR_MEMORY;
    }
    child->entry = parent->entry;
    child->user_stack_top = parent->user_stack_top;
    child->state = PROCESS_READY;
    u64 enqueue_flags = irq_save();
    ready_enqueue_locked(child);
    irq_restore(enqueue_flags);
    *child_pid = child->pid;
    return PROCESS_ERROR_NONE;
}

int process_exec(u64 pid, const void *image, u64 image_size) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state == PROCESS_RUNNING || image == NULL) {
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
    vmm_destroy_address_space(&process->space);
    process->space = replacement;
    process->entry = entry;
    process->user_stack_top = PROCESS_STACK_TOP;
    return PROCESS_ERROR_NONE;
}

int process_exec_current(const void *image, u64 image_size, u64 *entry, u64 *stack) {
    struct process *process = process_find(current_process_pid);
    if (process == NULL || process->state != PROCESS_RUNNING || image == NULL ||
        entry == NULL || stack == NULL) {
        return PROCESS_ERROR_INVALID;
    }
    struct address_space replacement;
    if (vmm_create_address_space(&replacement) != 0) {
        return PROCESS_ERROR_MEMORY;
    }
    u64 new_entry = 0;
    if (elf_load_into(&replacement, image, image_size, &new_entry) != ELF_OK) {
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
    u64 previous_entry = process->entry;
    u64 previous_stack = process->user_stack_top;
    process->space = replacement;
    process->entry = new_entry;
    process->user_stack_top = PROCESS_STACK_TOP;
    if (vmm_switch_address_space(&process->space) != 0) {
        process->space = previous;
        process->entry = previous_entry;
        process->user_stack_top = previous_stack;
        vmm_destroy_address_space(&replacement);
        return PROCESS_ERROR_MEMORY;
    }
    vmm_destroy_address_space(&previous);
    *entry = new_entry;
    *stack = PROCESS_STACK_TOP;
    return PROCESS_ERROR_NONE;
}

int process_exit(u64 pid, u64 status) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state == PROCESS_UNUSED) {
        return PROCESS_ERROR_INVALID;
    }
    if (process->state == PROCESS_RUNNING) {
        return PROCESS_ERROR_BUSY;
    }
    u64 flags = irq_save();
    if (process->state == PROCESS_READY) {
        ready_remove_locked(process);
    }
    process->state = PROCESS_ZOMBIE;
    process->exit_status = status;
    process->exited_ticks = timer_ticks();
    irq_restore(flags);
    return PROCESS_ERROR_NONE;
}

int process_wait(u64 parent_pid, u64 child_pid, u64 *status) {
    struct process *child = NULL;
    u64 flags = irq_save();
    for (u32 index = 0; index < MAX_PROCESSES; index++) {
        struct process *candidate = &process_table[index];
        if (!candidate->used || candidate->parent_pid != parent_pid) {
            continue;
        }
        if (child_pid != 0 && candidate->pid != child_pid) {
            continue;
        }
        if (candidate->state == PROCESS_ZOMBIE) {
            child = candidate;
            break;
        }
    }
    if (child == NULL) {
        irq_restore(flags);
        return PROCESS_ERROR_NO_CHILD;
    }
    u64 pid = child->pid;
    if (status != NULL) {
        *status = child->exit_status;
    }
    vmm_destroy_address_space(&child->space);
    process_release(child);
    irq_restore(flags);
    return (int)pid;
}

struct process *process_take_ready(void) {
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

int process_run(u64 pid, u64 *exit_code) {
    struct process *process = process_find(pid);
    if (process == NULL || process->state != PROCESS_READY) {
        return PROCESS_ERROR_NOT_READY;
    }
    u64 flags = irq_save();
    ready_remove_locked(process);
    process->state = PROCESS_RUNNING;
    current_process_pid = process->pid;
    irq_restore(flags);
    u64 code = 0;
    int result = user_run_in_address_space(&process->space, process->entry,
                                           process->user_stack_top, &code);
    u64 finish_flags = irq_save();
    current_process_pid = 0;
    process->state = PROCESS_ZOMBIE;
    process->exit_status = code;
    process->exited_ticks = timer_ticks();
    irq_restore(finish_flags);
    if (exit_code != NULL) {
        *exit_code = code;
    }
    return result;
}

u64 process_current_pid(void) {
    return current_process_pid;
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

int process_switch_space(struct process *process) {
    if (process == NULL) {
        vmm_switch_kernel();
        return PROCESS_ERROR_NONE;
    }
    return vmm_switch_address_space(&process->space);
}
