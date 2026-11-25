#include "vmm.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "spinlock.h"
#include "string.h"

#define PAGE_TABLE_ENTRIES 512
#define ADDRESS_MASK PTE_ADDRESS

static spinlock_t vmm_lock = SPINLOCK_INIT;
static u64 mapped_pages = 0;
static u64 boot_pml4_physical = 0;
static u64 high_pml4_entry = 0;
static u64 device_pd_physical = 0;
static bool high_mapping_ready = false;

static u64 *table_from_entry(u64 entry) {
    return (u64 *)(entry & ADDRESS_MASK);
}

static int allocate_table(u64 **table, u64 flags) {
    u64 frame = pmm_alloc_frame();
    if (frame == 0) {
        return -1;
    }
    *table = (u64 *)frame;
    memset(*table, 0, PAGE_SIZE);
    (void)flags;
    return 0;
}

static int ensure_table(u64 *entry, u64 flags, u64 **table) {
    if ((*entry & PTE_PRESENT) == 0) {
        u64 *new_table = NULL;
        if (allocate_table(&new_table, flags) != 0) {
            return -1;
        }
        *entry = (u64)new_table | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
        *table = new_table;
        return 0;
    }
    if (flags & PTE_USER) {
        *entry |= PTE_USER;
    }
    *table = table_from_entry(*entry);
    return 0;
}

static int split_huge_page(u64 *pde, u64 virtual_address, u64 flags) {
    u64 old = *pde;
    u64 *page_table = NULL;
    if (allocate_table(&page_table, flags) != 0) {
        return -1;
    }
    u64 base = old & 0x000FFFFFFFE00000ULL;
    u64 page_flags = (old & 0xFFF) & ~PTE_HUGE;
    page_flags &= ~PTE_USER;
    for (u64 index = 0; index < PAGE_TABLE_ENTRIES; index++) {
        page_table[index] = (base + index * PAGE_SIZE) | page_flags | PTE_PRESENT;
    }
    u64 table_flags = PTE_PRESENT | PTE_WRITABLE;
    if (flags & PTE_USER) {
        table_flags |= PTE_USER;
    }
    *pde = (u64)page_table | table_flags;
    tlb_flush(virtual_address & ~(PAGE_SIZE - 1));
    return 0;
}

static u64 *walk_to_pte(u64 virtual_address, u64 flags, bool create) {
    u64 pml4_physical = vmm_pml4_phys();
    u64 *pml4 = (u64 *)pml4_physical;
    u64 pml4_index = (virtual_address >> 39) & 0x1FF;
    u64 pdpt_index = (virtual_address >> 30) & 0x1FF;
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    u64 pt_index = (virtual_address >> 12) & 0x1FF;

    if (create) {
        u64 *pdpt = NULL;
        if (ensure_table(&pml4[pml4_index], flags, &pdpt) != 0) {
            return NULL;
        }
        u64 *pd = NULL;
        if (ensure_table(&pdpt[pdpt_index], flags, &pd) != 0) {
            return NULL;
        }
        if (pd[pd_index] & PTE_HUGE) {
            if (split_huge_page(&pd[pd_index], virtual_address, flags) != 0) {
                return NULL;
            }
        }
        u64 *pt = NULL;
        if (ensure_table(&pd[pd_index], flags, &pt) != 0) {
            return NULL;
        }
        return &pt[pt_index];
    }

    if ((pml4[pml4_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    u64 *pdpt = table_from_entry(pml4[pml4_index]);
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    if (pdpt[pdpt_index] & PTE_HUGE) {
        return NULL;
    }
    u64 *pd = table_from_entry(pdpt[pdpt_index]);
    if ((pd[pd_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    if (pd[pd_index] & PTE_HUGE) {
        return NULL;
    }
    u64 *pt = table_from_entry(pd[pd_index]);
    return &pt[pt_index];
}

void vmm_init(void) {
    mapped_pages = 0;
    u64 pml4 = vmm_pml4_phys();
    if (pml4 == 0 || (pml4 & (PAGE_SIZE - 1)) != 0) {
        panic("invalid page table root");
    }
    boot_pml4_physical = pml4;
    u64 high_pdpt_frame = pmm_alloc_frame();
    u64 high_pd_frame = pmm_alloc_frame();
    if (high_pdpt_frame == 0 || high_pd_frame == 0) {
        panic("failed to allocate kernel high-half page tables");
    }
    u64 *high_pdpt = (u64 *)high_pdpt_frame;
    u64 *high_pd = (u64 *)high_pd_frame;
    memset(high_pdpt, 0, PAGE_SIZE);
    memset(high_pd, 0, PAGE_SIZE);
    high_pdpt[510] = high_pd_frame | PTE_PRESENT | PTE_WRITABLE;
    for (u64 index = 0; index < PAGE_TABLE_ENTRIES; index++) {
        high_pd[index] = (index * 0x200000ULL) | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
    }
    u64 device_pd_frame = pmm_alloc_frame();
    if (device_pd_frame == 0) {
        panic("failed to allocate shared device window page tables");
    }
    memset((void *)(usize)device_pd_frame, 0, PAGE_SIZE);
    high_pdpt[509] = device_pd_frame | PTE_PRESENT | PTE_WRITABLE;
    device_pd_physical = device_pd_frame;
    u64 *pml4_table = (u64 *)pml4;
    pml4_table[511] = high_pdpt_frame | PTE_PRESENT | PTE_WRITABLE;
    high_pml4_entry = pml4_table[511];
    high_mapping_ready = true;
    tlb_flush(KERNEL_HIGH_BASE);
    kprintf("VMM: PML4 at %p, identity map below 1 GiB\n", (void *)pml4);
    kprintf("VMM: shared kernel high-half direct map at %p\n", (void *)KERNEL_HIGH_BASE);
    kprintf("VMM: shared device window at %p\n", (void *)VMM_DEVICE_BASE);
}

int vmm_map_device_page(u64 virtual_address, u64 physical_address, u64 flags) {
    if (device_pd_physical == 0) {
        return -1;
    }
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || (physical_address & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    if (virtual_address < VMM_DEVICE_BASE || virtual_address >= VMM_DEVICE_LIMIT) {
        return -1;
    }
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *device_pd = (u64 *)device_pd_physical;
    if ((device_pd[pd_index] & PTE_PRESENT) == 0) {
        u64 pt_frame = pmm_alloc_frame();
        if (pt_frame == 0) {
            spin_unlock_irqrestore(&vmm_lock, lock_flags);
            return -1;
        }
        memset((void *)(usize)pt_frame, 0, PAGE_SIZE);
        device_pd[pd_index] = pt_frame | PTE_PRESENT | PTE_WRITABLE;
    }
    u64 *pt = table_from_entry(device_pd[pd_index]);
    pt[pt_index] = (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PTE_PRESENT;
    tlb_flush(virtual_address);
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

u64 vmm_device_base(void) {
    return VMM_DEVICE_BASE;
}

u64 vmm_boot_pml4(void) {
    return boot_pml4_physical;
}

u64 vmm_high_pml4_entry(void) {
    return high_pml4_entry;
}

u64 vmm_pml4_phys(void) {
    return read_cr3() & ADDRESS_MASK;
}

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags) {
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || (physical_address & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    if (virtual_address >= 0x0000800000000000ULL) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *pte = walk_to_pte(virtual_address, flags, true);
    if (pte == NULL) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    if ((*pte & PTE_PRESENT) == 0) {
        mapped_pages++;
    }
    *pte = (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PTE_PRESENT;
    tlb_flush(virtual_address);
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

int vmm_map_range(u64 virtual_address, u64 physical_address, u64 page_count, u64 flags) {
    for (u64 index = 0; index < page_count; index++) {
        if (vmm_map_page(virtual_address + index * PAGE_SIZE, physical_address + index * PAGE_SIZE, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

int vmm_unmap_page(u64 virtual_address) {
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= 0x0000800000000000ULL) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *pte = walk_to_pte(virtual_address, 0, false);
    if (pte == NULL || (*pte & PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    *pte = 0;
    if (mapped_pages > 0) {
        mapped_pages--;
    }
    tlb_flush(virtual_address);
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

u64 vmm_get_physical(u64 virtual_address) {
    u64 pml4 = vmm_pml4_phys();
    u64 *pml4_table = (u64 *)pml4;
    u64 pml4_index = (virtual_address >> 39) & 0x1FF;
    if ((pml4_table[pml4_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    u64 *pdpt = table_from_entry(pml4_table[pml4_index]);
    u64 pdpt_index = (virtual_address >> 30) & 0x1FF;
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    if (pdpt[pdpt_index] & PTE_HUGE) {
        return (pdpt[pdpt_index] & 0x000FFFFFC0000000ULL) + (virtual_address & 0x3FFFFFFFULL);
    }
    u64 *pd = table_from_entry(pdpt[pdpt_index]);
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    if ((pd[pd_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    if (pd[pd_index] & PTE_HUGE) {
        return (pd[pd_index] & 0x000FFFFFFFE00000ULL) + (virtual_address & 0x1FFFFFULL);
    }
    u64 *pt = table_from_entry(pd[pd_index]);
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    if ((pt[pt_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    return (pt[pt_index] & ADDRESS_MASK) + (virtual_address & 0xFFF);
}

bool vmm_is_mapped(u64 virtual_address) {
    return vmm_get_physical(virtual_address) != 0;
}

bool vmm_user_range_valid(u64 address, u64 length) {
    if (length == 0) {
        return true;
    }
    if (address >= 0x0000800000000000ULL || address + length < address) {
        return false;
    }
    u64 first = address & ~(PAGE_SIZE - 1);
    u64 last = (address + length - 1) & ~(PAGE_SIZE - 1);
    for (u64 page = first; page <= last; page += PAGE_SIZE) {
        u64 pml4 = vmm_pml4_phys();
        u64 *pml4_table = (u64 *)pml4;
        u64 pml4_index = (page >> 39) & 0x1FF;
        if ((pml4_table[pml4_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pdpt = table_from_entry(pml4_table[pml4_index]);
        u64 pdpt_index = (page >> 30) & 0x1FF;
        if ((pdpt[pdpt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pd = table_from_entry(pdpt[pdpt_index]);
        u64 pd_index = (page >> 21) & 0x1FF;
        if ((pd[pd_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        if (pd[pd_index] & PTE_HUGE) {
            continue;
        }
        u64 *pt = table_from_entry(pd[pd_index]);
        u64 pt_index = (page >> 12) & 0x1FF;
        if ((pt[pt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
    }
    return true;
}

int vmm_create_address_space(struct address_space *space) {
    if (space == NULL || !high_mapping_ready) {
        return -1;
    }
    memset(space, 0, sizeof(*space));
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 pml4_frame = pmm_alloc_frame();
    u64 pdpt_frame = pmm_alloc_frame();
    u64 user_pd_frame = pmm_alloc_frame();
    if (pml4_frame == 0 || pdpt_frame == 0 || user_pd_frame == 0) {
        if (pml4_frame != 0) {
            pmm_free_frame(pml4_frame);
        }
        if (pdpt_frame != 0) {
            pmm_free_frame(pdpt_frame);
        }
        if (user_pd_frame != 0) {
            pmm_free_frame(user_pd_frame);
        }
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    u64 *pml4 = (u64 *)pml4_frame;
    u64 *pdpt = (u64 *)pdpt_frame;
    u64 *user_pd = (u64 *)user_pd_frame;
    memset(pml4, 0, PAGE_SIZE);
    memset(pdpt, 0, PAGE_SIZE);
    memset(user_pd, 0, PAGE_SIZE);
    u64 *kernel_pml4 = (u64 *)boot_pml4_physical;
    if ((kernel_pml4[0] & PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        pmm_free_frame(user_pd_frame);
        pmm_free_frame(pdpt_frame);
        pmm_free_frame(pml4_frame);
        return -1;
    }
    u64 *kernel_pdpt = table_from_entry(kernel_pml4[0]);
    if ((kernel_pdpt[0] & PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        pmm_free_frame(user_pd_frame);
        pmm_free_frame(pdpt_frame);
        pmm_free_frame(pml4_frame);
        return -1;
    }
    u64 kernel_low_pd = kernel_pdpt[0] & ADDRESS_MASK;
    pml4[0] = pdpt_frame | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pdpt[0] = kernel_low_pd | PTE_PRESENT | PTE_WRITABLE;
    pdpt[1] = user_pd_frame | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pml4[511] = high_pml4_entry & ~PTE_USER;
    if ((kernel_pdpt[1] & PTE_PRESENT) != 0 && (kernel_pdpt[1] & PTE_HUGE) == 0) {
        u64 *kernel_user_pd = table_from_entry(kernel_pdpt[1]);
        for (u32 index = 0; index < VMM_HEAP_PD_COUNT; index++) {
            u32 pd_index = VMM_HEAP_FIRST_PD + index;
            user_pd[pd_index] = kernel_user_pd[pd_index] & ~PTE_USER;
        }
    }
    space->pml4_phys = pml4_frame;
    space->pdpt_phys = pdpt_frame;
    space->user_pd_phys = user_pd_frame;
    space->ready = true;
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

int vmm_destroy_address_space(struct address_space *space) {
    if (space == NULL || !space->ready) {
        return -1;
    }
    if (space->pml4_phys == vmm_pml4_phys() || space->pml4_phys == boot_pml4_physical) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *pml4 = (u64 *)space->pml4_phys;
    u64 *pdpt = (u64 *)space->pdpt_phys;
    u64 *user_pd = (u64 *)space->user_pd_phys;
    (void)pml4;
    (void)pdpt;
    for (u32 pd_index = 0; pd_index < VMM_HEAP_FIRST_PD; pd_index++) {
        u64 entry = user_pd[pd_index];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_HUGE) != 0) {
            continue;
        }
        u64 *pt = table_from_entry(entry);
        for (u32 pt_index = 0; pt_index < PAGE_TABLE_ENTRIES; pt_index++) {
            u64 pte = pt[pt_index];
            if ((pte & PTE_PRESENT) != 0) {
                pmm_free_frame(pte & ADDRESS_MASK);
            }
        }
        pmm_free_frame((u64)pt);
        user_pd[pd_index] = 0;
    }
    pmm_free_frame(space->user_pd_phys);
    pmm_free_frame(space->pdpt_phys);
    pmm_free_frame(space->pml4_phys);
    memset(space, 0, sizeof(*space));
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

int vmm_clone_address_space(const struct address_space *source, struct address_space *destination) {
    if (source == NULL || destination == NULL || !source->ready) {
        return -1;
    }
    if (vmm_create_address_space(destination) != 0) {
        return -1;
    }
    u64 *source_pd = (u64 *)source->user_pd_phys;
    u64 *destination_pd = (u64 *)destination->user_pd_phys;
    for (u32 pd_index = 0; pd_index < VMM_HEAP_FIRST_PD; pd_index++) {
        u64 source_entry = source_pd[pd_index];
        if ((source_entry & PTE_PRESENT) == 0) {
            continue;
        }
        if ((source_entry & PTE_HUGE) != 0) {
            vmm_destroy_address_space(destination);
            return -1;
        }
        u64 *source_pt = table_from_entry(source_entry);
        u64 new_pt_frame = pmm_alloc_frame();
        if (new_pt_frame == 0) {
            vmm_destroy_address_space(destination);
            return -1;
        }
        u64 *new_pt = (u64 *)new_pt_frame;
        memset(new_pt, 0, PAGE_SIZE);
        for (u32 pt_index = 0; pt_index < PAGE_TABLE_ENTRIES; pt_index++) {
            u64 source_pte = source_pt[pt_index];
            if ((source_pte & PTE_PRESENT) == 0) {
                continue;
            }
            u64 new_frame = pmm_alloc_frame();
            if (new_frame == 0) {
                for (u32 free_index = 0; free_index < PAGE_TABLE_ENTRIES; free_index++) {
                    u64 pte = new_pt[free_index];
                    if ((pte & PTE_PRESENT) != 0) {
                        pmm_free_frame(pte & ADDRESS_MASK);
                    }
                }
                pmm_free_frame(new_pt_frame);
                vmm_destroy_address_space(destination);
                return -1;
            }
            memcpy((void *)new_frame, (void *)(source_pte & ADDRESS_MASK), PAGE_SIZE);
            new_pt[pt_index] = new_frame | (source_pte & 0xFFF) | PTE_PRESENT;
        }
        destination_pd[pd_index] = new_pt_frame | (source_entry & 0xFFF) | PTE_PRESENT | PTE_WRITABLE;
    }
    return 0;
}

int vmm_switch_address_space(const struct address_space *space) {
    if (space == NULL || !space->ready || (space->pml4_phys & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    write_cr3(space->pml4_phys);
    return 0;
}

void vmm_switch_kernel(void) {
    if (boot_pml4_physical != 0) {
        write_cr3(boot_pml4_physical);
    }
}

int vmm_map_user_page(struct address_space *space, u64 virtual_address, u64 physical_address, u64 flags) {
    if (space == NULL || !space->ready) {
        return -1;
    }
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || (physical_address & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    if (virtual_address < VMM_USER_SPACE_BASE || virtual_address >= VMM_USER_SPACE_LIMIT) {
        return -1;
    }
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    if (pd_index >= VMM_HEAP_FIRST_PD) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *user_pd = (u64 *)space->user_pd_phys;
    if ((user_pd[pd_index] & PTE_PRESENT) == 0) {
        u64 pt_frame = pmm_alloc_frame();
        if (pt_frame == 0) {
            spin_unlock_irqrestore(&vmm_lock, lock_flags);
            return -1;
        }
        memset((void *)pt_frame, 0, PAGE_SIZE);
        user_pd[pd_index] = pt_frame | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else if ((user_pd[pd_index] & PTE_HUGE) != 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    } else {
        user_pd[pd_index] |= PTE_USER;
    }
    u64 *pt = table_from_entry(user_pd[pd_index]);
    pt[pt_index] = (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PTE_PRESENT;
    if (vmm_pml4_phys() == space->pml4_phys) {
        tlb_flush(virtual_address);
    }
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

int vmm_unmap_user_page(struct address_space *space, u64 virtual_address) {
    if (space == NULL || !space->ready) {
        return -1;
    }
    if ((virtual_address & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    if (pd_index >= VMM_HEAP_FIRST_PD) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *user_pd = (u64 *)space->user_pd_phys;
    if ((user_pd[pd_index] & PTE_PRESENT) == 0 || (user_pd[pd_index] & PTE_HUGE) != 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    u64 *pt = table_from_entry(user_pd[pd_index]);
    if ((pt[pt_index] & PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    pt[pt_index] = 0;
    if (vmm_pml4_phys() == space->pml4_phys) {
        tlb_flush(virtual_address);
    }
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

u64 vmm_user_physical(const struct address_space *space, u64 virtual_address) {
    if (space == NULL || !space->ready) {
        return 0;
    }
    u64 *pml4 = (u64 *)space->pml4_phys;
    u64 pml4_index = (virtual_address >> 39) & 0x1FF;
    if ((pml4[pml4_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    u64 *pdpt = table_from_entry(pml4[pml4_index]);
    u64 pdpt_index = (virtual_address >> 30) & 0x1FF;
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0 || (pdpt[pdpt_index] & PTE_HUGE) != 0) {
        return 0;
    }
    u64 *pd = table_from_entry(pdpt[pdpt_index]);
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    if ((pd[pd_index] & PTE_PRESENT) == 0 || (pd[pd_index] & PTE_HUGE) != 0) {
        return 0;
    }
    u64 *pt = table_from_entry(pd[pd_index]);
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    if ((pt[pt_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    return (pt[pt_index] & ADDRESS_MASK) + (virtual_address & 0xFFF);
}

bool vmm_user_range_valid_in(const struct address_space *space, u64 address, u64 length) {
    if (space == NULL || !space->ready) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (address >= 0x0000800000000000ULL || address + length < address) {
        return false;
    }
    u64 first = address & ~(PAGE_SIZE - 1);
    u64 last = (address + length - 1) & ~(PAGE_SIZE - 1);
    for (u64 page = first; page <= last; page += PAGE_SIZE) {
        u64 *pml4 = (u64 *)space->pml4_phys;
        u64 pml4_index = (page >> 39) & 0x1FF;
        if ((pml4[pml4_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pdpt = table_from_entry(pml4[pml4_index]);
        u64 pdpt_index = (page >> 30) & 0x1FF;
        if ((pdpt[pdpt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pd = table_from_entry(pdpt[pdpt_index]);
        u64 pd_index = (page >> 21) & 0x1FF;
        if ((pd[pd_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        if ((pd[pd_index] & PTE_HUGE) != 0) {
            continue;
        }
        u64 *pt = table_from_entry(pd[pd_index]);
        u64 pt_index = (page >> 12) & 0x1FF;
        if ((pt[pt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
    }
    return true;
}
