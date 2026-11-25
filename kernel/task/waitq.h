#ifndef Z_OS_WAITQ_H
#define Z_OS_WAITQ_H

#include "types.h"

#define WAITQ_MAX 32

void waitq_init(void);
int waitq_block(u64 waiting_pid, u64 target_pid, u64 deadline_ticks);
void waitq_remove(u64 waiting_pid);
u32 waitq_wake_target(u64 child_pid);
u32 waitq_expire(u64 now_ticks);
u32 waitq_count(void);
u64 waitq_deadline(u64 waiting_pid);

#endif
