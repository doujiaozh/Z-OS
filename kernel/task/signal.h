#ifndef Z_OS_SIGNAL_H
#define Z_OS_SIGNAL_H

#include "types.h"

#define SIGNAL_MAX 32

#define SIGNAL_SIGHUP 1
#define SIGNAL_SIGINT 2
#define SIGNAL_SIGQUIT 3
#define SIGNAL_SIGKILL 9
#define SIGNAL_SIGUSR1 10
#define SIGNAL_SIGSEGV 11
#define SIGNAL_SIGUSR2 12
#define SIGNAL_SIGALRM 14
#define SIGNAL_SIGTERM 15
#define SIGNAL_SIGCHLD 17
#define SIGNAL_SIGCONT 18
#define SIGNAL_SIGSTOP 19

#define SIGNAL_FRAME_SIZE 176

struct registers;
struct process;

struct signal_state {
    u64 pending;
    u64 blocked;
    u64 handler[SIGNAL_MAX];
    u64 trampoline;
    u32 installed;
    u32 delivered;
};

void signal_init(struct signal_state *state);
u64 signal_install(struct signal_state *state, u64 signum, u64 handler, u64 trampoline);
bool signal_raise(struct signal_state *state, u64 signum);
u64 signal_mask(struct signal_state *state, u64 mask, u32 operation);
u64 signal_pending_unblocked(const struct signal_state *state);
bool signal_has_deliverable(const struct signal_state *state);
bool signal_is_blocked(const struct signal_state *state, u64 signum);
bool signal_restore(struct registers *frame, const void *context, u64 length);
int signal_deliver(struct process *process, struct registers *frame);
u64 signal_default_action(u64 signum);

#endif
