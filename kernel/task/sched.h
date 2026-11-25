#ifndef Z_OS_TASK_SCHED_H
#define Z_OS_TASK_SCHED_H

#include "types.h"

#define SCHED_MAX_BATCH 8
#define SCHED_MARK_SLOTS 192
#define SCHED_MARK_KINDS 64

#define SCHED_ERROR_NONE 0
#define SCHED_ERROR_INVALID -1
#define SCHED_ERROR_BUSY -2
#define SCHED_ERROR_DEADLOCK -3

void sched_process_init(void);
int sched_run_batch(const u64 *pids, u32 count, u32 *completed);
u64 sched_interrupt_return(u64 frame_rsp);
void sched_user_tick(void);
bool sched_user_active(void);
u64 sched_preemption_count(void);
u64 sched_quantum_ticks(void);
u64 sched_switch_count_of(u64 pid);
void sched_exit_current(u64 status) __attribute__((noreturn));
void sched_terminate_current(u64 status) __attribute__((noreturn));
void sched_record_mark(u64 marker);
u32 sched_mark_count(void);
bool sched_mark_get(u32 index, u64 *pid, u64 *marker);
u64 sched_marker_total(u64 marker);
void sched_mark_reset(void);

#endif
