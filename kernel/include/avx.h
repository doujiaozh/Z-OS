#ifndef Z_OS_AVX_H
#define Z_OS_AVX_H

#include "types.h"

#define PROCESS_XSAVE_SIZE 1024

void cpu_avx_init(void);
bool cpu_avx_active(void);
u64 cpu_avx_area_size(void);
void arch_xsave(void *area);
void arch_xrstor(void *area);

#endif
