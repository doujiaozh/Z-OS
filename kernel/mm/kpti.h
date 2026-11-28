#ifndef Z_OS_KPTI_H
#define Z_OS_KPTI_H

#include "types.h"

struct address_space;

extern void kpti_probe_entry(void);
extern u64 kpti_probe_switch(u64 restricted_cr3, u64 kernel_cr3);
extern void kpti_probe_enter(u64 restricted_cr3, u64 kernel_cr3);
extern u64 kpti_kernel_cr3_slot;
extern u64 kpti_probe_hit;
extern u64 kpti_probe_seen;
extern u8 kpti_probe_stack[];
extern u8 kpti_probe_stack_top[];

int kpti_build_view(void);
void kpti_init(void);
bool kpti_enabled(void);
bool kpti_built(void);
bool kpti_active(void);
u64 kpti_kernel_cr3(void);
u64 kpti_user_cr3(void);
u64 kpti_trampoline_virtual(void);
u64 kpti_trampoline_physical(void);
u32 kpti_trampoline_pages(void);
u32 kpti_view_table_pages(void);
bool kpti_view_maps(u64 virtual_address);
u32 kpti_view_present_pages(void);
int kpti_enter_kernel(void);
int kpti_leave_kernel(void);
u64 kpti_switch_count(void);
int kpti_run_switch_probe(void);
int kpti_probe_active_view(void);
int kpti_run_entry_probe(void);
bool kpti_probe_hit_flag(void);
u64 kpti_probe_original_cr3(void);
u64 kpti_probe_stack_virtual(void);
int kpti_seal_process(struct address_space *space);
u32 kpti_user_kernel_pages(struct address_space *space);

#endif
