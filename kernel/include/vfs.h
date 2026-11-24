#ifndef Z_OS_VFS_H
#define Z_OS_VFS_H

#include "types.h"

#define VFS_NAME_MAX 32
#define VFS_PATH_MAX 256
#define VFS_MAX_NODES 128
#define VFS_MAX_FILE_SIZE (1024 * 1024)

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIRECTORY 2
#define VFS_TYPE_DEVICE 3

#define VFS_DEVICE_NULL 1
#define VFS_DEVICE_CONSOLE 2
#define VFS_DEVICE_RANDOM 3

#define VFS_OK 0
#define VFS_ERROR_NOT_FOUND -1
#define VFS_ERROR_EXISTS -2
#define VFS_ERROR_INVALID -3
#define VFS_ERROR_NO_SPACE -4
#define VFS_ERROR_NOT_EMPTY -5
#define VFS_ERROR_TYPE -6

struct vfs_node;

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    u32 type;
    u64 size;
};

struct vfs_stat {
    u32 type;
    u64 size;
    u32 child_count;
    u32 device_id;
};

struct vfs_stats {
    u32 nodes_used;
    u64 bytes_allocated;
};

void vfs_init(void);
struct vfs_node *vfs_root(void);
struct vfs_node *vfs_lookup(struct vfs_node *base, const char *path);
int vfs_create(struct vfs_node *base, const char *path);
int vfs_mkdir(struct vfs_node *base, const char *path);
int vfs_remove(struct vfs_node *base, const char *path);
int vfs_rename(struct vfs_node *base, const char *old_path, const char *new_path);
int vfs_read(struct vfs_node *node, u64 offset, void *buffer, u64 length);
int vfs_write(struct vfs_node *node, u64 offset, const void *buffer, u64 length);
int vfs_append(struct vfs_node *node, const void *buffer, u64 length);
int vfs_truncate(struct vfs_node *node);
u32 vfs_list(struct vfs_node *directory, struct vfs_dirent *buffer, u32 capacity);
int vfs_stat(struct vfs_node *node, struct vfs_stat *stat);
int vfs_build_path(struct vfs_node *node, char *buffer, usize size);
const char *vfs_node_name(struct vfs_node *node);
bool vfs_is_directory(struct vfs_node *node);
bool vfs_is_file(struct vfs_node *node);
struct vfs_stats vfs_get_stats(void);

#endif
