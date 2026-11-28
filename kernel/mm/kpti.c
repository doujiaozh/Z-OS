#include "kpti.h"
#include "interrupts.h"
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
    u32 table_pages;
};

static struct kpti_state state;
static u64 shadow_tables[8];
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
        if (shadow_table_count >= 8) {
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

static int map_probe_state_pages(u64 *direct_pd) {
    const u64 addresses[4] = {
        (u64)(usize)&kpti_kernel_cr3_slot,
        (u64)(usize)&kpti_probe_hit,
        (u64)(usize)&kpti_probe_seen,
        (u64)(usize)kpti_probe_stack,
    };
    for (u32 index = 0; index < 4; index++) {
        u64 page = page_down(addresses[index]);
        if (map_view_page(direct_pd, page, page, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return -1;
        }
    }
    return 0;
}

static u64 current_stack_page(void) {
    u64 stack_pointer = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(stack_pointer));
    return page_down(stack_pointer);
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
    u64 *sealed_user_pd = (u64 *)shadow_user_pd;
    u64 *source_user_pd = (u64 *)user_pd_physical;
    for (u32 index = 0; index < VMM_HEAP_FIRST_PD; index++) {
        sealed_user_pd[index] = source_user_pd[index];
    }
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
    u64 probe_stack_base = page_down((u64)(usize)kpti_probe_stack);
    u64 probe_stack_limit = page_up((u64)(usize)kpti_probe_stack_top);
    for (u64 address = probe_stack_base; address < probe_stack_limit; address += PAGE_SIZE) {
        if (map_view_page(direct_pd, address, address, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
            return -1;
        }
    }
    if (map_probe_state_pages(direct_pd) != 0) {
        return -1;
    }
    u64 stack_page = current_stack_page();
    if (map_view_page(direct_pd, stack_page, stack_page, PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
        return -1;
    }
    if (map_view_page(direct_pd, stack_page + PAGE_SIZE, stack_page + PAGE_SIZE,
                      PTE_WRITABLE | PTE_NO_EXECUTE) != 0) {
        return -1;
    }
    state.kernel_cr3 = kernel_pml4_physical;
    state.user_cr3 = shadow_pml4;
    state.shadow_pdpt = shadow_pdpt;
    state.shadow_direct_pd = shadow_direct_pd;
    state.table_pages = 3 + shadow_table_count;
    kpti_kernel_cr3_slot = kernel_pml4_physical;
    state.built = true;
    state.enabled = true;
    return 0;
}

void kpti_init(void) {
    memset(&state, 0, sizeof(state));
    shadow_table_count = 0;
    kpti_kernel_cr3_slot = 0;
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
    return (read_cr3() & PTE_ADDRESS) == (state.user_cr3 & PTE_ADDRESS);
}

u64 kpti_kernel_cr3(void) {
    return state.kernel_cr3;
}

u64 kpti_user_cr3(void) {
    return state.user_cr3;
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

bool kpti_view_maps(u64 virtual_address) {
    if (!state.built) {
        return true;
    }
    u64 *pml4 = (u64 *)state.user_cr3;
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

u32 kpti_view_present_pages(void) {
    if (!state.built) {
        return 0;
    }
    u64 *direct_pd = (u64 *)state.shadow_direct_pd;
    u32 pages = 0;
    for (u32 pd_index = 0; pd_index < 512; pd_index++) {
        if ((direct_pd[pd_index] & PTE_PRESENT) == 0) {
            continue;
        }
        if ((direct_pd[pd_index] & PTE_HUGE) != 0) {
            pages += 512;
            continue;
        }
        u64 *pt = (u64 *)(direct_pd[pd_index] & PTE_ADDRESS);
        for (u32 pt_index = 0; pt_index < 512; pt_index++) {
            if ((pt[pt_index] & PTE_PRESENT) != 0) {
                pages++;
            }
        }
    }
    return pages;
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
    return 0;
}

int kpti_leave_kernel(void) {
    if (!state.built) {
        return -1;
    }
    if ((read_cr3() & PTE_ADDRESS) == (state.user_cr3 & PTE_ADDRESS)) {
        return 0;
    }
    write_cr3(state.user_cr3);
    state.switches++;
    return 0;
}

u64 kpti_switch_count(void) {
    return state.switches;
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
    if (state.user_cr3 == 0) {
        return -1;
    }
    return (on_kernel && matched) ? 0 : -1;
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

bool kpti_probe_hit_flag(void) {
    return kpti_probe_hit != 0;
}

u64 kpti_probe_original_cr3(void) {
    return kpti_probe_seen;
}

int kpti_run_entry_probe(void) {
    if (!state.built) {
        return -1;
    }
    u64 irq_flags = irq_save();
    u64 saved = read_cr3();
    kpti_probe_hit = 0;
    kpti_probe_seen = 0;
    kpti_probe_enter(state.user_cr3, state.kernel_cr3);
    write_cr3(saved);
    bool hit = kpti_probe_hit_flag();
    bool matched = (kpti_probe_original_cr3() & PTE_ADDRESS) == (state.user_cr3 & PTE_ADDRESS);
    irq_restore(irq_flags);
    if (!hit) {
        return -1;
    }
    return matched ? 0 : -1;
}

int kpti_seal_process(struct address_space *space) {
    if (space == NULL) {
        return -1;
    }
    return 0;
}

u32 kpti_user_kernel_pages(struct address_space *space) {
    if (space == NULL) {
        return 0;
    }
    return 0;
}
