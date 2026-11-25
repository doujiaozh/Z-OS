#ifndef Z_OS_FD_H
#define Z_OS_FD_H

#include "types.h"

#define FD_MAX 16

#define FD_TYPE_NONE 0
#define FD_TYPE_CONSOLE 1
#define FD_TYPE_VFS 2
#define FD_TYPE_NULL 3

#define FD_FLAG_READ 0x1
#define FD_FLAG_WRITE 0x2
#define FD_FLAG_APPEND 0x4

#define FD_ERROR_INVALID -1
#define FD_ERROR_NO_SPACE -2
#define FD_ERROR_MODE -3
#define FD_ERROR_IO -4

struct vfs_node;

struct fd_entry {
    bool used;
    u8 type;
    u8 flags;
    u64 offset;
    struct vfs_node *node;
};

struct fd_table {
    struct fd_entry entries[FD_MAX];
};

void fd_table_init(struct fd_table *table);
int fd_alloc(struct fd_table *table, u8 type, u8 flags, struct vfs_node *node);
int fd_close(struct fd_table *table, int fd);
int fd_dup(struct fd_table *table, int fd);
int fd_copy(struct fd_table *destination, const struct fd_table *source);
int fd_get(struct fd_table *table, int fd, struct fd_entry **entry);
int fd_read(struct fd_table *table, int fd, void *buffer, u64 length);
int fd_write(struct fd_table *table, int fd, const void *buffer, u64 length);
int fd_open_path(struct fd_table *table, struct vfs_node *base, const char *path, u8 flags);
int fd_seek(struct fd_table *table, int fd, u64 offset);
u32 fd_count(const struct fd_table *table);

#endif
