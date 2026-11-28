#ifndef Z_OS_ZDISK_H
#define Z_OS_ZDISK_H

#include "types.h"

#define ZDISK_MAX_FILES 32
#define ZDISK_NAME_MAX 32

#define ZDISK_ERROR_NONE 0
#define ZDISK_ERROR_ABSENT -1
#define ZDISK_ERROR_FORMAT -2
#define ZDISK_ERROR_RANGE -3
#define ZDISK_ERROR_MEMORY -4

struct zdisk_file {
    char name[ZDISK_NAME_MAX];
    u64 size;
    u64 first_lba;
};

int zdisk_init(void);
bool zdisk_mounted(void);
u32 zdisk_file_count(void);
u64 zdisk_part_lba(void);
u64 zdisk_part_sectors(void);
const struct zdisk_file *zdisk_lookup(const char *name);
const struct zdisk_file *zdisk_lookup_index(u32 index);
int zdisk_read(const char *name, void *buffer, u64 capacity, u64 *read_bytes);
int zdisk_load(const char *name, void **buffer, u64 *size);
const char *zdisk_error_text(int error);

#endif
