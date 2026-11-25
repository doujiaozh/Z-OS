#include "cred.h"

void cred_init_root(struct credentials *cred) {
    if (cred == NULL) {
        return;
    }
    cred->uid = CRED_ROOT_UID;
    cred->gid = CRED_ROOT_GID;
    cred->euid = CRED_ROOT_UID;
    cred->egid = CRED_ROOT_GID;
    cred->privileged = true;
}

void cred_init(struct credentials *cred, u32 uid, u32 gid) {
    if (cred == NULL) {
        return;
    }
    cred->uid = uid;
    cred->gid = gid;
    cred->euid = uid;
    cred->egid = gid;
    cred->privileged = uid == CRED_ROOT_UID;
}

void cred_inherit(const struct credentials *source, struct credentials *destination) {
    if (source == NULL || destination == NULL) {
        return;
    }
    *destination = *source;
}

bool cred_is_root(const struct credentials *cred) {
    return cred != NULL && cred->euid == CRED_ROOT_UID;
}

bool cred_can_access(const struct credentials *cred, u32 owner_uid, u32 owner_gid,
                     u32 owner_mode, u32 group_mode, u32 other_mode, u32 access) {
    if (cred == NULL || access == 0) {
        return false;
    }
    if (cred->euid == CRED_ROOT_UID) {
        return true;
    }
    u32 mode;
    if (cred->euid == owner_uid) {
        mode = owner_mode;
    } else if (cred->egid == owner_gid) {
        mode = group_mode;
    } else {
        mode = other_mode;
    }
    return (mode & access) == access;
}

bool cred_seteuid(struct credentials *cred, u32 euid) {
    if (cred == NULL) {
        return false;
    }
    if (!cred->privileged && euid != cred->uid && euid != cred->euid) {
        return false;
    }
    cred->euid = euid;
    cred->privileged = euid == CRED_ROOT_UID;
    return true;
}

bool cred_setegid(struct credentials *cred, u32 egid) {
    if (cred == NULL) {
        return false;
    }
    if (!cred->privileged && egid != cred->gid && egid != cred->egid) {
        return false;
    }
    cred->egid = egid;
    return true;
}

u32 cred_effective_uid(const struct credentials *cred) {
    return cred != NULL ? cred->euid : CRED_ROOT_UID;
}

u32 cred_effective_gid(const struct credentials *cred) {
    return cred != NULL ? cred->egid : CRED_ROOT_GID;
}

bool cred_switch_user(struct credentials *cred, u32 uid, u32 gid) {
    if (cred == NULL) {
        return false;
    }
    if (!cred->privileged) {
        return false;
    }
    cred->uid = uid;
    cred->gid = gid;
    cred->euid = uid;
    cred->egid = gid;
    cred->privileged = uid == CRED_ROOT_UID;
    return true;
}
