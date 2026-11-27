#ifndef Z_OS_SECURITY_H
#define Z_OS_SECURITY_H

#include "types.h"

#define SECURITY_CR4_SMEP (1ULL << 20)
#define SECURITY_CR4_SMAP (1ULL << 21)

void security_init(void);
bool security_smep_supported(void);
bool security_smap_supported(void);
bool security_smep_active(void);
bool security_smap_active(void);
u64 security_entropy(void);
u64 security_random(void);
void __stack_chk_fail(void);
void security_init_canary(void);
u64 security_canary(void);
bool security_nx_supported(void);
bool security_nx_active(void);
void security_stac(void);
void security_clac(void);

#endif
