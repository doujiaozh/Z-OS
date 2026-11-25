#ifndef Z_OS_CRED_H
#define Z_OS_CRED_H

#include "types.h"

#define CRED_MODE_EXEC 0x1
#define CRED_MODE_WRITE 0x2
#define CRED_MODE_READ 0x4

#define CRED_ROOT_UID 0
#define CRED_ROOT_GID 0

struct credentials {
    u32 uid;
    u32 gid;
    u32 euid;
    u32 egid;
    bool privileged;
};

void cred_init_root(struct credentials *cred);
void cred_init(struct credentials *cred, u32 uid, u32 gid);
void cred_inherit(const struct credentials *source, struct credentials *destination);
bool cred_can_access(const struct credentials *cred, u32 owner_uid, u32 owner_gid,
                     u32 owner_mode, u32 group_mode, u32 other_mode, u32 access);
bool cred_is_root(const struct credentials *cred);
bool cred_seteuid(struct credentials *cred, u32 euid);
bool cred_setegid(struct credentials *cred, u32 egid);
u32 cred_effective_uid(const struct credentials *cred);
u32 cred_effective_gid(const struct credentials *cred);
bool cred_switch_user(struct credentials *cred, u32 uid, u32 gid);

#endif
