#include "fd.h"
#include "console.h"
#include "string.h"
#include "vfs.h"

void fd_table_init(struct fd_table *table) {
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(struct fd_table));
    table->entries[0].used = true;
    table->entries[0].type = FD_TYPE_CONSOLE;
    table->entries[0].flags = FD_FLAG_READ;
    table->entries[1].used = true;
    table->entries[1].type = FD_TYPE_CONSOLE;
    table->entries[1].flags = FD_FLAG_WRITE;
    table->entries[2].used = true;
    table->entries[2].type = FD_TYPE_CONSOLE;
    table->entries[2].flags = FD_FLAG_WRITE;
}

int fd_alloc(struct fd_table *table, u8 type, u8 flags, struct vfs_node *node) {
    if (table == NULL || type == FD_TYPE_NONE) {
        return FD_ERROR_INVALID;
    }
    for (int index = 0; index < FD_MAX; index++) {
        if (table->entries[index].used) {
            continue;
        }
        table->entries[index].used = true;
        table->entries[index].type = type;
        table->entries[index].flags = flags;
        table->entries[index].offset = 0;
        table->entries[index].node = node;
        return index;
    }
    return FD_ERROR_NO_SPACE;
}

int fd_close(struct fd_table *table, int fd) {
    if (table == NULL || fd < 0 || fd >= FD_MAX || !table->entries[fd].used) {
        return FD_ERROR_INVALID;
    }
    memset(&table->entries[fd], 0, sizeof(struct fd_entry));
    return 0;
}

int fd_dup(struct fd_table *table, int fd) {
    if (table == NULL || fd < 0 || fd >= FD_MAX || !table->entries[fd].used) {
        return FD_ERROR_INVALID;
    }
    struct fd_entry copy = table->entries[fd];
    for (int index = 0; index < FD_MAX; index++) {
        if (table->entries[index].used) {
            continue;
        }
        table->entries[index] = copy;
        table->entries[index].used = true;
        return index;
    }
    return FD_ERROR_NO_SPACE;
}

int fd_copy(struct fd_table *destination, const struct fd_table *source) {
    if (destination == NULL || source == NULL) {
        return FD_ERROR_INVALID;
    }
    *destination = *source;
    return 0;
}

int fd_get(struct fd_table *table, int fd, struct fd_entry **entry) {
    if (table == NULL || fd < 0 || fd >= FD_MAX || !table->entries[fd].used) {
        return FD_ERROR_INVALID;
    }
    if (entry != NULL) {
        *entry = &table->entries[fd];
    }
    return 0;
}

int fd_read(struct fd_table *table, int fd, void *buffer, u64 length) {
    struct fd_entry *entry = NULL;
    if (fd_get(table, fd, &entry) != 0 || buffer == NULL || length == 0) {
        return FD_ERROR_INVALID;
    }
    if ((entry->flags & FD_FLAG_READ) == 0) {
        return FD_ERROR_MODE;
    }
    if (entry->type == FD_TYPE_CONSOLE) {
        return 0;
    }
    if (entry->type == FD_TYPE_NULL) {
        return 0;
    }
    if (entry->type != FD_TYPE_VFS || entry->node == NULL) {
        return FD_ERROR_INVALID;
    }
    int result = vfs_read(entry->node, entry->offset, buffer, length);
    if (result > 0) {
        entry->offset += (u64)result;
    }
    return result;
}

int fd_write(struct fd_table *table, int fd, const void *buffer, u64 length) {
    struct fd_entry *entry = NULL;
    if (fd_get(table, fd, &entry) != 0 || buffer == NULL || length == 0) {
        return FD_ERROR_INVALID;
    }
    if ((entry->flags & FD_FLAG_WRITE) == 0) {
        return FD_ERROR_MODE;
    }
    if (entry->type == FD_TYPE_CONSOLE) {
        console_write_n((const char *)buffer, (usize)length);
        return (int)length;
    }
    if (entry->type == FD_TYPE_NULL) {
        return (int)length;
    }
    if (entry->type != FD_TYPE_VFS || entry->node == NULL) {
        return FD_ERROR_INVALID;
    }
    int result;
    if ((entry->flags & FD_FLAG_APPEND) != 0) {
        result = vfs_append(entry->node, buffer, length);
    } else {
        result = vfs_write(entry->node, entry->offset, buffer, length);
    }
    if (result > 0) {
        entry->offset += (u64)result;
    }
    return result;
}

int fd_open_path(struct fd_table *table, struct vfs_node *base, const char *path, u8 flags) {
    if (table == NULL || path == NULL || (flags & (FD_FLAG_READ | FD_FLAG_WRITE)) == 0) {
        return FD_ERROR_INVALID;
    }
    struct vfs_node *node = vfs_lookup(base, path);
    if (node == NULL) {
        return FD_ERROR_INVALID;
    }
    if (vfs_is_directory(node)) {
        return FD_ERROR_MODE;
    }
    return fd_alloc(table, FD_TYPE_VFS, flags, node);
}

int fd_seek(struct fd_table *table, int fd, u64 offset) {
    struct fd_entry *entry = NULL;
    if (fd_get(table, fd, &entry) != 0) {
        return FD_ERROR_INVALID;
    }
    entry->offset = offset;
    return 0;
}

u32 fd_count(const struct fd_table *table) {
    if (table == NULL) {
        return 0;
    }
    u32 count = 0;
    for (int index = 0; index < FD_MAX; index++) {
        if (table->entries[index].used) {
            count++;
        }
    }
    return count;
}
