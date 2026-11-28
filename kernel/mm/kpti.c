#include "kpti.h"
#include "gdt.h"
#include "kstack.h"
#include "interrupts.h"
#include "kstack.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

extern u8 __trampoline_start[];
extern u8 __trampoline_end[];

struct kpti_state {
    bool built;
    bool enabled;
    u64 kernel_cr3;
    u64 user_cr3;
    u64 shadow_pdpt;
    u64 shadow_direct_pd;
    u64 trampoline_physical;
    u64 trampoline_pages;
    u64 switches;
    u64 entry_switches;
    u64 return_switches;
    u64 view_cr3;
    u32 table_pages;
};

static struct kpti_state state;
static u64 shadow_tables[24];
static u32 shadow_table_count = 0;

static u64 page_down(u64 value) {
    return value & ~(PAGE_SIZE - 1);
}

static u64 page_up(u64 value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static u64 zeroed_frame(void) {
    u64 frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }
    memset((void *)(usize)frame, 0, PAGE_SIZE);
    return frame;
}

static int map_view_page(u64 *direct_pd, u64 virtual_address, u64 physical_address, u64 flags) {
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);
    u64 *page_table = NULL;
    if ((direct_pd[pd_index] & PTE_PRESENT) == 0) {
        if (shadow_table_count >= 24) {
            return -1;
        }
        u64 frame = zeroed_frame();
        if (frame == 0) {
            return -1;
        }
        shadow_tables[shadow_table_count++] = frame;
        page_table = (u64 *)frame;
        direct_pd[pd_index] = frame | PTE_PRESENT | PTE_WRITABLE;
    } else {
        page_table = (u64 *)(direct_pd[pd_index] & PTE_ADDRESS);
    }
    page_table[pt_index] = (physical_address & PTE_ADDRESS) |
                           (flags & (0xFFF | PTE_NO_EXECUTE)) | PTE_PRESENT;
    return 0;
}

static int seal_user_directory(u64 *sealed_pd, u64 source_pd_physical) {
    u64 *source = (u64 *)source_pd_physical;
    for (u32 index = 0; index < VMM_HEAP_FIRST_PD; index++) {
        sealed_pd[index] = source[index];
    }
    return 0;
}

int kpti_build_view(void) {
    if (state.built) {
        return 0;
    }
    shadow_table_count = 0;
    u64 kernel_pml4_physical = vmm_pml4_phys();
    u64 *kernel_pml4 = (u64 *)kernel_pml4_physical;
    if ((kernel_pml4[0] & PTE_PRESENT) == 0 || (kernel_pml4[0] & PTE_HUGE) != 0) {
        return -1;
    }
    u64 *kernel_pdpt = (u64 *)(kernel_pml4[0] & PTE_ADDRESS);
    if ((kernel_pdpt[1] & PTE_PRESENT) == 0) {
        return -1;
    }
    u64 user_pd_physical = kernel_pdpt[1] & PTE_ADDRESS;
    u64 shadow_pml4 = zeroed_frame();
    u64 shadow_pdpt = zeroed_frame();
    u64 shadow_direct_pd = zeroed_frame();
    u64 shadow_user_pd = zeroed_frame();
    if (shadow_pml4 == 0 || shadow_pdpt == 0 || shadow_direct_pd == 0 || shadow_user_pd == 0) {
        return -1;
    }
    seal_user_directory((u64 *)shadow_user_pd, user_pd_physical);
    u64 *pml4 = (u64 *)shadow_pml4;
    u64 *pdpt = (u64 *)shadow_pdpt;
    u64 *direct_pd = (u64 *)shadow_direct_pd;
    pdpt[0] = shadow_direct_pd | PTE_PRESENT | PTE_WRITABLE;
    pdpt[1] = shadow_user_pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pml4[0] = shadow_pdpt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    u64 trampoline_start = page_down((u64)(usize)__trampoline_start);
    u64 trampoline_end = page_up((u64)(usize)__trampoline_end);
    state.trampoline_pages = (trampoline_end - trampoline_start) / PAGE_SIZE;
    if (state.trampoline_pages == 0) {
        state.trampoline_pages = 1;
    }
    state.trampoline_physical = trampoline_start;
    for (u64 index = 0; index < state.trampoline_pages; index++) {
        u64 address = trampoline_start + index * PAGE_SIZE;
        if (map_view_page(direct_pd, address, address, 0) != 0) {
            return -1;
        }
    }
    u64 idt_base = page_down(interrupt_idt_base());
    if (map_view_page(direct_pd, idt_base, idt_base, 0) != 0) {
        return -1;
    }
    u64 return_stack_base = page_down((u64)(usize)kpti_return_stack);
    u64 return_stack_limit = page_up((u64)(usize)kpti_return_stack_top);
    for (u64 address = return_stack_base; address < return_stack_limit; address += PAGE_SIZE) {
        if (map_view_page(direct_pd, address, address, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return -1;
        }
    }
    u64 gdt_base = page_down(gdt_base_address());
    if (map_view_page(direct_pd, gdt_base, gdt_base, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
        return -1;
    }
    const u64 state_pages[4] = {
        (u64)(usize)&kpti_kernel_cr3_slot,
        (u64)(usize)&kpti_return_cr3_slot,
        (u64)(usize)&kpti_probe_hit,
        (u64)(usize)&kpti_probe_seen,
    };
    for (u32 index = 0; index < 4; index++) {
        u64 page = page_down(state_pages[index]);
        if (map_view_page(direct_pd, page, page, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return -1;
        }
    }
    u64 probe_stack_base = page_down((u64)(usize)kpti_probe_stack);
    u64 probe_stack_limit = page_up((u64)(usize)kpti_probe_stack_top);
    for (u64 address = probe_stack_base; address < probe_stack_limit; address += PAGE_SIZE) {
        if (map_view_page(direct_pd, address, address, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return -1;
        }
    }
    u64 window_pd = kstack_window_pd();
    if (window_pd != 0) {
        u64 view_high_pdpt = zeroed_frame();
        if (view_high_pdpt == 0) {
            return -1;
        }
        u64 *view_pdpt = (u64 *)view_high_pdpt;
        view_pdpt[KSTACK_WINDOW_PDPT_SLOT] = window_pd | PTE_PRESENT | PTE_WRITABLE;
        pml4[511] = view_high_pdpt | PTE_PRESENT | PTE_WRITABLE;
    }
    state.kernel_cr3 = kernel_pml4_physical;
    state.user_cr3 = shadow_pml4;
    state.shadow_pdpt = shadow_pdpt;
    state.shadow_direct_pd = shadow_direct_pd;
    state.table_pages = 3 + shadow_table_count + 1;
    kpti_kernel_cr3_slot = kernel_pml4_physical;
    kpti_return_cr3_slot = shadow_pml4;
    kpti_frame_page_slot = kstack_frame_page();
    state.built = true;
    state.enabled = true;
    return 0;
}

void kpti_init(void) {
    memset(&state, 0, sizeof(state));
    shadow_table_count = 0;
    kpti_kernel_cr3_slot = 0;
    kpti_return_cr3_slot = 0;
    kpti_probe_hit = 0;
    kpti_probe_seen = 0;
    kpti_build_view();
}

bool kpti_enabled(void) {
    return state.enabled;
}

bool kpti_built(void) {
    return state.built;
}

bool kpti_active(void) {
    if (!state.built) {
        return false;
    }
    u64 current = read_cr3() & PTE_ADDRESS;
    if (current == (state.user_cr3 & PTE_ADDRESS)) {
        return true;
    }
    return false;
}

u64 kpti_kernel_cr3(void) {
    return state.kernel_cr3;
}

u64 kpti_user_cr3(void) {
    return state.user_cr3;
}

u64 kpti_active_view_cr3(void) {
    return state.view_cr3;
}

u64 kpti_return_slot_value(void) {
    return kpti_return_cr3_slot;
}

u64 kpti_view_counter(void) {
    return state.entry_switches + state.return_switches;
}

u64 kpti_entry_switch_count(void) {
    return state.entry_switches;
}

u64 kpti_return_count(void) {
    return state.return_switches;
}

u64 kpti_trampoline_virtual(void) {
    return page_down((u64)(usize)__trampoline_start);
}

u64 kpti_trampoline_physical(void) {
    return state.trampoline_physical;
}

u32 kpti_trampoline_pages(void) {
    return (u32)state.trampoline_pages;
}

u32 kpti_view_table_pages(void) {
    return state.table_pages;
}

u64 kpti_probe_stack_virtual(void) {
    return page_down((u64)(usize)kpti_probe_stack);
}

static bool walk_maps(u64 cr3, u64 virtual_address) {
    u64 *pml4 = (u64 *)cr3;
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    if ((pml4[pml4_index] & PTE_PRESENT) == 0) {
        return false;
    }
    u64 *pdpt = (u64 *)(pml4[pml4_index] & PTE_ADDRESS);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0) {
        return false;
    }
    if ((pdpt[pdpt_index] & PTE_HUGE) != 0) {
        return true;
    }
    u64 *pd = (u64 *)(pdpt[pdpt_index] & PTE_ADDRESS);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    if ((pd[pd_index] & PTE_PRESENT) == 0) {
        return false;
    }
    if ((pd[pd_index] & PTE_HUGE) != 0) {
        return true;
    }
    u64 *pt = (u64 *)(pd[pd_index] & PTE_ADDRESS);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);
    return (pt[pt_index] & PTE_PRESENT) != 0;
}

static u32 count_present(u64 cr3) {
    u64 *pml4 = (u64 *)cr3;
    u32 pages = 0;
    for (u32 pml4_index = 0; pml4_index < 512; pml4_index++) {
        if ((pml4[pml4_index] & PTE_PRESENT) == 0) {
            continue;
        }
        u64 *pdpt = (u64 *)(pml4[pml4_index] & PTE_ADDRESS);
        for (u32 pdpt_index = 0; pdpt_index < 512; pdpt_index++) {
            u64 pdpt_entry = pdpt[pdpt_index];
            if ((pdpt_entry & PTE_PRESENT) == 0) {
                continue;
            }
            if ((pdpt_entry & PTE_HUGE) != 0) {
                pages += 512;
                continue;
            }
            u64 *pd = (u64 *)(pdpt_entry & PTE_ADDRESS);
            for (u32 pd_index = 0; pd_index < 512; pd_index++) {
                u64 pd_entry = pd[pd_index];
                if ((pd_entry & PTE_PRESENT) == 0) {
                    continue;
                }
                if ((pd_entry & PTE_HUGE) != 0) {
                    pages += 512;
                    continue;
                }
                u64 *pt = (u64 *)(pd_entry & PTE_ADDRESS);
                for (u32 pt_index = 0; pt_index < 512; pt_index++) {
                    if ((pt[pt_index] & PTE_PRESENT) != 0) {
                        pages++;
                    }
                }
            }
        }
    }
    return pages;
}

bool kpti_view_maps(u64 virtual_address) {
    if (!state.built) {
        return true;
    }
    return walk_maps(state.user_cr3, virtual_address);
}

u32 kpti_view_present_pages(void) {
    if (!state.built) {
        return 0;
    }
    return count_present(state.user_cr3);
}

int kpti_enter_kernel(void) {
    if (!state.built) {
        return -1;
    }
    if ((read_cr3() & PTE_ADDRESS) == (state.kernel_cr3 & PTE_ADDRESS)) {
        return 0;
    }
    write_cr3(state.kernel_cr3);
    state.switches++;
    state.entry_switches++;
    return 0;
}

int kpti_leave_kernel(void) {
    if (!state.built) {
        return -1;
    }
    u64 target = state.user_cr3;
    if ((read_cr3() & PTE_ADDRESS) == (target & PTE_ADDRESS)) {
        return 0;
    }
    write_cr3(target);
    state.switches++;
    state.return_switches++;
    return 0;
}

u64 kpti_switch_count(void) {
    return state.switches;
}

void kpti_blend_slots(u64 kernel_cr3, u64 user_cr3) {
    if (kernel_cr3 != 0) {
        kpti_kernel_cr3_slot = kernel_cr3;
    }
    state.view_cr3 = user_cr3;
    kpti_return_cr3_slot = user_cr3;
}

void kpti_clear_return_slot(void) {
    state.view_cr3 = 0;
    kpti_return_cr3_slot = 0;
}

void kpti_select_view(struct address_space *space) {
    if (space == NULL || !space->ready) {
        kpti_clear_return_slot();
        return;
    }
    kpti_kernel_cr3_slot = space->pml4_phys;
}

int kpti_build_process_view(struct address_space *space, u64 kernel_stack_base,
                           u32 kernel_stack_pages, struct kpti_view *view) {
    if (space == NULL || view == NULL || kernel_stack_base == 0) {
        return -1;
    }
    memset(view, 0, sizeof(struct kpti_view));
    if (!space->ready || space->user_pd_phys == 0) {
        return -1;
    }
    u64 user_pd_physical = space->user_pd_phys;
    u64 pml4_frame = zeroed_frame();
    u64 pdpt_frame = zeroed_frame();
    u64 direct_pd_frame = zeroed_frame();
    u64 user_pd_frame = zeroed_frame();
    if (pml4_frame == 0 || pdpt_frame == 0 || direct_pd_frame == 0 || user_pd_frame == 0) {
        return -1;
    }
    view->frames[view->frame_count++] = pml4_frame;
    view->frames[view->frame_count++] = pdpt_frame;
    view->frames[view->frame_count++] = direct_pd_frame;
    view->frames[view->frame_count++] = user_pd_frame;
    seal_user_directory((u64 *)user_pd_frame, user_pd_physical);
    u64 *pml4 = (u64 *)pml4_frame;
    u64 *pdpt = (u64 *)pdpt_frame;
    u64 *direct_pd = (u64 *)direct_pd_frame;
    pdpt[0] = direct_pd_frame | PTE_PRESENT | PTE_WRITABLE;
    pdpt[1] = user_pd_frame | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pml4[0] = pdpt_frame | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    u32 saved_tables = shadow_table_count;
    shadow_table_count = 0;
    u64 point_addresses[10];
    u32 point_count = 0;
    u64 trampoline_start = page_down((u64)(usize)__trampoline_start);
    u64 trampoline_end = page_up((u64)(usize)__trampoline_end);
    for (u64 address = trampoline_start; address < trampoline_end; address += PAGE_SIZE) {
        if (map_view_page(direct_pd, address, address, 0) != 0) {
            shadow_table_count = saved_tables;
            return -1;
        }
    }
    point_addresses[point_count++] = page_down(interrupt_idt_base());
    point_addresses[point_count++] = page_down(gdt_base_address());
    point_addresses[point_count++] = page_down((u64)(usize)&kpti_kernel_cr3_slot);
    point_addresses[point_count++] = page_down((u64)(usize)&kpti_return_cr3_slot);
    point_addresses[point_count++] = page_down((u64)(usize)kpti_probe_stack);
    for (u32 index = 0; index < point_count; index++) {
        u64 page = point_addresses[index];
        u64 flags = (page == page_down(interrupt_idt_base())) ? 0 : (PTE_WRITABLE | PTE_NO_EXECUTE);
        if (page == page_down((u64)(usize)kpti_probe_stack)) {
            flags = PTE_WRITABLE | PTE_NO_EXECUTE;
        }
        if (map_view_page(direct_pd, page, page, flags) != 0) {
            shadow_table_count = saved_tables;
            return -1;
        }
    }
    u64 process_window_pd = kstack_window_pd();
    if (process_window_pd != 0) {
        u64 process_high_pdpt = zeroed_frame();
        if (process_high_pdpt == 0) {
            shadow_table_count = saved_tables;
            return -1;
        }
        u64 *process_pdpt = (u64 *)process_high_pdpt;
        process_pdpt[KSTACK_WINDOW_PDPT_SLOT] = process_window_pd | PTE_PRESENT | PTE_WRITABLE;
        pml4[511] = process_high_pdpt | PTE_PRESENT | PTE_WRITABLE;
        if (view->frame_count < KPTI_VIEW_FRAMES) {
            view->frames[view->frame_count++] = process_high_pdpt;
        }
    }
    (void)kernel_stack_base;
    (void)kernel_stack_pages;
    for (u32 index = 0; index < shadow_table_count; index++) {
        if (view->frame_count >= KPTI_VIEW_FRAMES) {
            break;
        }
        view->frames[view->frame_count++] = shadow_tables[index];
    }
    shadow_table_count = saved_tables;
    view->cr3 = pml4_frame;
    view->built = true;
    return 0;
}

void kpti_destroy_view(struct kpti_view *view) {
    if (view == NULL || !view->built) {
        return;
    }
    for (u32 index = 0; index < view->frame_count; index++) {
        if (view->frames[index] != 0) {
            pmm_free_frame(view->frames[index]);
        }
    }
    memset(view, 0, sizeof(struct kpti_view));
}

bool kpti_view_maps_for(const struct kpti_view *view, u64 virtual_address) {
    if (view == NULL || !view->built) {
        return false;
    }
    return walk_maps(view->cr3, virtual_address);
}

u32 kpti_view_present_pages_for(const struct kpti_view *view) {
    if (view == NULL || !view->built) {
        return 0;
    }
    return count_present(view->cr3);
}

int kpti_run_switch_probe(void) {
    if (!state.built) {
        return -1;
    }
    u64 irq_flags = irq_save();
    u64 saved = read_cr3();
    u64 observed = kpti_probe_switch(state.user_cr3, state.kernel_cr3);
    kpti_probe_seen = observed;
    write_cr3(saved);
    bool matched = (observed & PTE_ADDRESS) == (state.user_cr3 & PTE_ADDRESS);
    bool restored = (read_cr3() & PTE_ADDRESS) == (saved & PTE_ADDRESS);
    irq_restore(irq_flags);
    if (!matched) {
        return -1;
    }
    return restored ? 0 : -1;
}

int kpti_probe_active_view(void) {
    if (!state.built) {
        return -1;
    }
    u64 irq_flags = irq_save();
    u64 saved = read_cr3();
    kpti_enter_kernel();
    bool on_kernel = !kpti_active();
    u64 probe = kpti_probe_switch(state.user_cr3, state.kernel_cr3);
    write_cr3(saved);
    bool matched = (probe & PTE_ADDRESS) == (state.user_cr3 & PTE_ADDRESS);
    irq_restore(irq_flags);
    return (on_kernel && matched) ? 0 : -1;
}

bool kpti_probe_hit_flag(void) {
    return kpti_probe_hit != 0;
}

u64 kpti_probe_original_cr3(void) {
    return kpti_probe_seen;
}

int kpti_seal_process(struct address_space *space) {
    if (space == NULL || !space->ready) {
        return -1;
    }
    return 0;
}

u32 kpti_user_kernel_pages(struct address_space *space) {
    if (space == NULL || !space->ready) {
        return 0;
    }
    return count_present(space->pml4_phys);
}
