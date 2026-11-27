#ifndef Z_OS_VMM_H
#define Z_OS_VMM_H

#include "types.h"

#define PTE_PRESENT 0x001ULL
#define PTE_WRITABLE 0x002ULL
#define PTE_USER 0x004ULL
#define PTE_WRITE_THROUGH 0x008ULL
#define PTE_CACHE_DISABLE 0x010ULL
#define PTE_ACCESSED 0x020ULL
#define PTE_DIRTY 0x040ULL
#define PTE_HUGE 0x080ULL
#define PTE_GLOBAL 0x100ULL
#define PTE_ADDRESS 0x000FFFFFFFFFF000ULL
#define PTE_NO_EXECUTE 0x8000000000000000ULL

#define KERNEL_HIGH_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_HIGH_LIMIT 0xFFFFFFFFC0000000ULL
#define VMM_DEVICE_BASE 0xFFFFFFFF40000000ULL
#define VMM_DEVICE_LIMIT 0xFFFFFFFF80000000ULL
#define VMM_USER_SPACE_BASE 0x40000000ULL
#define VMM_USER_SPACE_LIMIT 0x40100000ULL
#define VMM_USER_STACK_BASE 0x40010000ULL
#define VMM_USER_STACK_TOP 0x40010FF8ULL
#define VMM_HEAP_FIRST_PD 256
#define VMM_HEAP_PD_COUNT 8

struct address_space {
    u64 pml4_phys;
    u64 pdpt_phys;
    u64 user_pd_phys;
    bool ready;
};

void vmm_init(void);
u64 vmm_pml4_phys(void);
u64 vmm_boot_pml4(void);
u64 vmm_high_pml4_entry(void);
int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags);
int vmm_map_range(u64 virtual_address, u64 physical_address, u64 page_count, u64 flags);
int vmm_unmap_page(u64 virtual_address);
u64 vmm_get_physical(u64 virtual_address);
u64 vmm_query_flags(u64 virtual_address);
bool vmm_is_mapped(u64 virtual_address);
bool vmm_user_range_valid(u64 address, u64 length);
int vmm_create_address_space(struct address_space *space);
int vmm_destroy_address_space(struct address_space *space);
int vmm_clone_address_space(const struct address_space *source, struct address_space *destination);
int vmm_switch_address_space(const struct address_space *space);
void vmm_switch_kernel(void);
int vmm_map_user_page(struct address_space *space, u64 virtual_address, u64 physical_address, u64 flags);
int vmm_unmap_user_page(struct address_space *space, u64 virtual_address);
int vmm_map_device_page(u64 virtual_address, u64 physical_address, u64 flags);
u64 vmm_device_base(void);
u64 vmm_user_physical(const struct address_space *space, u64 virtual_address);
bool vmm_user_range_valid_in(const struct address_space *space, u64 address, u64 length);

#endif
