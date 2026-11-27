#include "avx.h"
#include "console.h"
#include "io.h"
#include "lib.h"

static bool avx_active = false;
static u64 avx_area_size = 0;

static void cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static void write_xcr0(u64 value) {
    u32 low = (u32)value;
    u32 high = (u32)(value >> 32);
    __asm__ volatile("xsetbv" : : "c"(0), "a"(low), "d"(high) : "memory");
}

static u64 read_xcr0(void) {
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0) : "memory");
    return ((u64)high << 32) | low;
}

void cpu_avx_init(void) {
    u32 eax = 0;
    u32 ebx = 0;
    u32 ecx = 0;
    u32 edx = 0;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    u32 maximum = eax;
    if (maximum < 1) {
        kprintf("cpu: avx unavailable (no cpuid leaf 1)\n");
        return;
    }
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    bool xsave = (ecx & (1U << 26)) != 0;
    bool avx = (ecx & (1U << 28)) != 0;
    bool fma = (ecx & (1U << 12)) != 0;
    bool f16c = (ecx & (1U << 29)) != 0;
    bool avx2 = false;
    if (maximum >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        avx2 = (ebx & (1U << 5)) != 0;
    }
    if (!(xsave && avx && avx2 && fma && f16c)) {
        kprintf("cpu: avx unsupported xsave=%u avx=%u avx2=%u fma=%u f16c=%u\n",
                xsave ? 1 : 0, avx ? 1 : 0, avx2 ? 1 : 0, fma ? 1 : 0, f16c ? 1 : 0);
        return;
    }
    cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
    u64 supported = ((u64)edx << 32) | eax;
    avx_area_size = ebx;
    if ((supported & 0x7) != 0x7 || avx_area_size == 0 || avx_area_size > PROCESS_XSAVE_SIZE) {
        kprintf("cpu: avx xsave state unsupported xcr0=%llx size=%llu\n",
                supported & 0xFFFFFFFFULL, avx_area_size);
        return;
    }
    u64 cr4 = read_cr4();
    cr4 |= (1ULL << 18) | (1ULL << 9) | (1ULL << 10);
    write_cr4(cr4);
    write_xcr0(7);
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    bool osxsave = (ecx & (1U << 27)) != 0;
    u64 xcr0 = read_xcr0();
    avx_active = osxsave && ((xcr0 & 0x7) == 0x7);
    kprintf("cpu: osxsave %s, avx2 %s, fma %s, f16c %s, xcr0=%llx, xsave=%llu bytes\n",
            osxsave ? "on" : "off", avx2 ? "on" : "off", fma ? "on" : "off",
            f16c ? "on" : "off", xcr0, avx_area_size);
}

bool cpu_avx_active(void) {
    return avx_active;
}

u64 cpu_avx_area_size(void) {
    return avx_area_size;
}
