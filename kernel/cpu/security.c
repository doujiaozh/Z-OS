#include "security.h"
#include "console.h"
#include "io.h"
#include "lib.h"

static bool smep_supported = false;
static bool nx_supported = false;
static bool nx_active = false;
static bool smap_supported = false;
static bool smep_active = false;
static bool smap_active = false;
static u64 entropy_state = 0x9E3779B97F4A7C15ULL;

static void cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static u64 read_timestamp(void) {
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

void security_init(void) {
    u32 eax = 0;
    u32 ebx = 0;
    u32 ecx = 0;
    u32 edx = 0;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    u32 maximum_leaf = eax;
    if (maximum_leaf >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        smep_supported = (ebx & (1U << 7)) != 0;
        smap_supported = (ebx & (1U << 20)) != 0;
    }
    cpuid(0x80000001U, 0, &eax, &ebx, &ecx, &edx);
    nx_supported = (edx & (1U << 20)) != 0;
    if (nx_supported) {
        u32 low = 0;
        u32 high = 0;
        __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080U));
        low |= 1U << 11;
        __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000080U));
        __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080U));
        nx_active = (low & (1U << 11)) != 0;
    }
    u64 cr4 = read_cr4();
    if (smep_supported) {
        cr4 |= SECURITY_CR4_SMEP;
        smep_active = true;
    }
    if (smap_supported) {
        cr4 |= SECURITY_CR4_SMAP;
        smap_active = true;
    }
    if (smep_supported || smap_supported) {
        write_cr4(cr4);
    }
    u64 cr4_after = read_cr4();
    smep_active = (cr4_after & SECURITY_CR4_SMEP) != 0;
    smap_active = (cr4_after & SECURITY_CR4_SMAP) != 0;
    entropy_state ^= read_timestamp();
    entropy_state ^= ((u64)maximum_leaf << 32) ^ (u64)edx;
    u64 cr0 = read_cr0();
    cr0 |= 1ULL << 16;
    write_cr0(cr0);
    kprintf("cpu: smep %s, smap %s, nx %s, cr4=%016llx\n",
            smep_active ? "on" : (smep_supported ? "failed" : "unsupported"),
            smap_active ? "on" : (smap_supported ? "failed" : "unsupported"),
            nx_active ? "on" : (nx_supported ? "failed" : "unsupported"),
            cr4_after);
}

bool security_nx_supported(void) {
    return nx_supported;
}

bool security_nx_active(void) {
    return nx_active;
}

bool security_smep_supported(void) {
    return smep_supported;
}

bool security_smap_supported(void) {
    return smap_supported;
}

bool security_smep_active(void) {
    return smep_active;
}

bool security_smap_active(void) {
    return smap_active;
}

u64 security_entropy(void) {
    entropy_state ^= read_timestamp();
    entropy_state *= 0x100000001B3ULL;
    entropy_state ^= entropy_state >> 29;
    return entropy_state;
}

u64 security_random(void) {
    entropy_state ^= entropy_state << 13;
    entropy_state ^= entropy_state >> 7;
    entropy_state ^= entropy_state << 17;
    return entropy_state;
}

void security_stac(void) {
    if (smap_active) {
        __asm__ volatile("stac" ::: "memory");
    }
}

void security_clac(void) {
    if (smap_active) {
        __asm__ volatile("clac" ::: "memory");
    }
}

u64 __stack_chk_guard = 0;

void security_init_canary(void) {
    u64 value = security_random();
    value &= 0xFFFFFFFFFFFFFF00ULL;
    value ^= read_timestamp() & 0xFF;
    if (value == 0) {
        value = 0x5A5A5A5A5A5A5A00ULL;
    }
    __stack_chk_guard = value;
}

u64 security_canary(void) {
    return __stack_chk_guard;
}

void __stack_chk_fail(void) {
    panic("stack smash detected");
}
