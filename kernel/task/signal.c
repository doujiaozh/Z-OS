#include "signal.h"
#include "interrupts.h"
#include "io.h"
#include "process.h"
#include "string.h"
#include "vmm.h"

#define SIGNAL_MASK_OPERATION_BLOCK 1
#define SIGNAL_MASK_OPERATION_UNBLOCK 2
#define SIGNAL_MASK_OPERATION_SET 3

void signal_init(struct signal_state *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(struct signal_state));
}

u64 signal_install(struct signal_state *state, u64 signum, u64 handler, u64 trampoline) {
    if (state == NULL || signum == 0 || signum >= SIGNAL_MAX || signum == SIGNAL_SIGKILL ||
        signum == SIGNAL_SIGSTOP) {
        return 0;
    }
    u64 previous = state->handler[signum];
    state->handler[signum] = handler;
    if (handler != 0) {
        state->installed++;
        if (trampoline != 0) {
            state->trampoline = trampoline;
        }
    }
    return previous;
}

bool signal_raise(struct signal_state *state, u64 signum) {
    if (state == NULL || signum == 0 || signum >= SIGNAL_MAX) {
        return false;
    }
    state->pending |= 1ULL << signum;
    return true;
}

u64 signal_mask(struct signal_state *state, u64 mask, u32 operation) {
    if (state == NULL) {
        return 0;
    }
    u64 previous = state->blocked;
    u64 allowed = mask & ~((1ULL << SIGNAL_SIGKILL) | (1ULL << SIGNAL_SIGSTOP));
    if (operation == SIGNAL_MASK_OPERATION_BLOCK) {
        state->blocked |= allowed;
    } else if (operation == SIGNAL_MASK_OPERATION_UNBLOCK) {
        state->blocked &= ~allowed;
    } else {
        state->blocked = allowed;
    }
    return previous;
}

u64 signal_pending_unblocked(const struct signal_state *state) {
    if (state == NULL) {
        return 0;
    }
    return state->pending & ~state->blocked;
}

bool signal_has_deliverable(const struct signal_state *state) {
    return signal_pending_unblocked(state) != 0;
}

bool signal_is_blocked(const struct signal_state *state, u64 signum) {
    if (state == NULL || signum >= SIGNAL_MAX) {
        return false;
    }
    return (state->blocked & (1ULL << signum)) != 0;
}

bool signal_restore(struct registers *frame, const void *context, u64 length) {
    if (frame == NULL || context == NULL || length < SIGNAL_FRAME_SIZE) {
        return false;
    }
    memcpy(frame, context, sizeof(struct registers));
    return true;
}

u64 signal_default_action(u64 signum) {
    if (signum == SIGNAL_SIGCHLD || signum == SIGNAL_SIGCONT) {
        return 0;
    }
    return 1;
}

int signal_deliver(struct process *process, struct registers *frame) {
    if (process == NULL || frame == NULL) {
        return 0;
    }
    struct signal_state *state = &process->signals;
    u64 deliverable = signal_pending_unblocked(state);
    if (deliverable == 0) {
        return 0;
    }
    u64 signum = 0;
    for (u64 bit = 1; bit < SIGNAL_MAX; bit++) {
        if ((deliverable & (1ULL << bit)) != 0) {
            signum = bit;
            break;
        }
    }
    if (signum == 0) {
        return 0;
    }
    state->pending &= ~(1ULL << signum);
    u64 handler = state->handler[signum];
    if (handler == 0) {
        return signal_default_action(signum) == 0 ? 0 : -1;
    }
    if (state->trampoline == 0) {
        return -1;
    }
    u64 user_stack = frame->rsp;
    if (user_stack <= SIGNAL_FRAME_SIZE + 8) {
        return -1;
    }
    u64 context_address = (user_stack - (SIGNAL_FRAME_SIZE + 8)) & ~0xFULL;
    if (!vmm_user_range_valid_in(&process->space, context_address, SIGNAL_FRAME_SIZE + 8)) {
        return -1;
    }
    u64 active_pml4 = vmm_pml4_phys();
    bool switched = false;
    if (active_pml4 != process->space.pml4_phys) {
        if (vmm_switch_address_space(&process->space) != 0) {
            return -1;
        }
        switched = true;
    }
    *(u64 *)(usize)context_address = state->trampoline;
    memcpy((void *)(usize)(context_address + 8), frame, sizeof(struct registers));
    if (switched) {
        write_cr3(active_pml4);
    }
    frame->rip = handler;
    frame->rsp = context_address;
    frame->rdi = signum;
    frame->rax = 0;
    state->delivered++;
    return 1;
}
