#ifndef Z_OS_KPTI_H
#define Z_OS_KPTI_H

#include "kstack.h"
#include "types.h"

#define KPTI_VIEW_FRAMES 24

struct address_space;

struct kpti_view {
    u64 cr3;
    u64 frames[KPTI_VIEW_FRAMES];
    u32 frame_count;
    bool built;
};

extern void kpti_probe_entry(void);
extern u64 kpti_probe_switch(u64 restricted_cr3, u64 kernel_cr3);
extern void kpti_probe_enter(u64 restricted_cr3, u64 kernel_cr3);
extern void kpti_return_to_user(void);
extern u64 kpti_kernel_cr3_slot;
extern u64 kpti_return_cr3_slot;
extern u64 kpti_probe_hit;
extern u64 kpti_probe_seen;
extern u8 kpti_return_stack[];
extern u8 kpti_return_stack_top[];
extern u64 kpti_probe_stack[];
extern u64 kpti_probe_stack_top[];
extern void *kpti_stub_table[256];

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
bool kpti_probe_hit_flag(void);
u64 kpti_probe_original_cr3(void);
u64 kpti_probe_stack_virtual(void);
int kpti_seal_process(struct address_space *space);
u32 kpti_user_kernel_pages(struct address_space *space);

void kpti_blend_slots(u64 kernel_cr3, u64 user_cr3);
void kpti_select_view(struct address_space *space);
void kpti_clear_return_slot(void);
u64 kpti_active_view_cr3(void);
u64 kpti_return_slot_value(void);
u64 kpti_view_counter(void);
u64 kpti_entry_switch_count(void);
u64 kpti_return_count(void);
int kpti_build_process_view(struct address_space *space, u64 kernel_stack_base,
                           u32 kernel_stack_pages, struct kpti_view *view);
void kpti_destroy_view(struct kpti_view *view);
bool kpti_view_maps_for(const struct kpti_view *view, u64 virtual_address);
u32 kpti_view_present_pages_for(const struct kpti_view *view);

#endif
