#include "user.h"
#include "console.h"
#include "gdt.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "thread.h"
#include "vmm.h"

#define USER_CODE_BASE 0x40000000ULL
#define USER_STACK_BASE 0x40001000ULL
#define USER_STACK_TOP 0x40002000ULL

extern u8 user_program_start[];
extern u8 user_program_end[];
extern void user_enter_and_wait(u64 entry, u64 stack);
extern void user_exit_to_kernel(void) __attribute__((noreturn));

static bool user_prepared = false;
static bool user_active = false;
static volatile u64 user_exit_code = 0;
static u8 user_interrupt_stack[16384] aligned(16);

static int prepare_user_memory(void) {
    if (user_prepared) {
        return 0;
    }
    u64 program_size = (u64)(user_program_end - user_program_start);
    if (program_size == 0 || program_size > PAGE_SIZE) {
        return -1;
    }
    u64 code_frame = pmm_alloc_frame();
    u64 stack_frame = pmm_alloc_frame();
    if (code_frame == 0 || stack_frame == 0) {
        if (code_frame != 0) {
            pmm_free_frame(code_frame);
        }
        if (stack_frame != 0) {
            pmm_free_frame(stack_frame);
        }
        return -2;
    }
    memcpy((void *)code_frame, user_program_start, (usize)program_size);
    if (vmm_map_page(USER_CODE_BASE, code_frame, PTE_PRESENT | PTE_USER) != 0) {
        pmm_free_frame(code_frame);
        pmm_free_frame(stack_frame);
        return -3;
    }
    if (vmm_map_page(USER_STACK_BASE, stack_frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
        vmm_unmap_page(USER_CODE_BASE);
        pmm_free_frame(code_frame);
        pmm_free_frame(stack_frame);
        return -3;
    }
    user_prepared = true;
    kprintf("User: mapped code at %p and stack at %p\n", (void *)USER_CODE_BASE, (void *)USER_STACK_BASE);
    return 0;
}

int user_run_demo(u64 *exit_code) {
    if (user_active) {
        return -4;
    }
    int result = prepare_user_memory();
    if (result != 0) {
        return result;
    }
    user_active = true;
    user_exit_code = 0xFFFFFFFFFFFFFFFFULL;
    sched_preempt_disable();
    u64 previous_stack = tss_get_rsp0();
    tss_set_rsp0((u64)user_interrupt_stack + sizeof(user_interrupt_stack));
    kprintf("User: entering ring 3\n");
    user_enter_and_wait(USER_CODE_BASE, USER_STACK_TOP);
    tss_set_rsp0(previous_stack);
    sched_preempt_enable();
    interrupts_enable();
    user_active = false;
    if (exit_code != NULL) {
        *exit_code = user_exit_code;
    }
    if (user_exit_code == 0xFFFFFFFFFFFFFFFFULL) {
        return -5;
    }
    return 0;
}

void user_finish(u64 status) {
    if (!user_active) {
        panic("user exit requested outside user mode");
    }
    user_exit_code = status;
    user_exit_to_kernel();
}

bool user_handle_exception(u64 vector) {
    if (!user_active) {
        return false;
    }
    console_set_color(COLOR_WHITE, COLOR_RED);
    kprintf("\nUser exception %llu in ring 3\n", vector);
    console_reset_color();
    user_exit_code = 0x100 | vector;
    user_exit_to_kernel();
    return true;
}
