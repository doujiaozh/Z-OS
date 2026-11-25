#ifndef Z_OS_ZFS_H
#define Z_OS_ZFS_H

#include "types.h"

#define ZFS_MAGIC 0x5A4F5346U
#define ZFS_VERSION 1
#define ZFS_BLOCK_SIZE 512
#define ZFS_MAX_FILES 16
#define ZFS_NAME_MAX 32
#define ZFS_MAX_FILE_SIZE (64 * 1024)
#define ZFS_SUPER_SECTOR 0
#define ZFS_TABLE_SECTOR 1
#define ZFS_TABLE_SECTORS 2
#define ZFS_DATA_START 64
#define ZFS_SECTORS_PER_FILE 128
#define ZFS_ERROR_NONE 0
#define ZFS_ERROR_ABSENT -1
#define ZFS_ERROR_FORMAT -2
#define ZFS_ERROR_NO_SPACE -3
#define ZFS_ERROR_NOT_FOUND -4
#define ZFS_ERROR_RANGE -5

struct zfs_dirent {
    char name[ZFS_NAME_MAX];
    u32 size;
    u32 first_sector;
};

int zfs_format(void);
int zfs_mount(void);
bool zfs_mounted(void);
u32 zfs_file_count(void);
u32 zfs_list(struct zfs_dirent *buffer, u32 capacity);
int zfs_write_file(const char *name, const void *data, u32 size);
int zfs_read_file(const char *name, void *buffer, u32 capacity);
int zfs_remove_file(const char *name);
int zfs_sync(void);
int zfs_mount_vfs(void);
int zfs_sync_vfs(void);
u32 zfs_disk_sectors(void);

#endif
