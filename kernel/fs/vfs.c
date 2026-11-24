#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "input.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "spinlock.h"
#include "string.h"
#include "timer.h"

struct vfs_node {
    bool used;
    u8 type;
    u8 reserved[3];
    u32 device_id;
    char name[VFS_NAME_MAX];
    u64 size;
    u64 capacity;
    u8 *data;
    struct vfs_node *parent;
    struct vfs_node *children;
    struct vfs_node *next_sibling;
    u64 created_tick;
};

static struct vfs_node nodes[VFS_MAX_NODES];
static struct vfs_node *root_node = NULL;
static spinlock_t vfs_lock = SPINLOCK_INIT;

static struct vfs_node *allocate_node(void) {
    for (u32 index = 0; index < VFS_MAX_NODES; index++) {
        if (!nodes[index].used) {
            memset(&nodes[index], 0, sizeof(struct vfs_node));
            nodes[index].used = true;
            nodes[index].created_tick = timer_ticks();
            return &nodes[index];
        }
    }
    return NULL;
}

static struct vfs_node *find_child(struct vfs_node *parent, const char *name) {
    if (parent == NULL || !parent->used || parent->type != VFS_TYPE_DIRECTORY) {
        return NULL;
    }
    struct vfs_node *child = parent->children;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

static bool valid_name(const char *name) {
    if (name == NULL || name[0] == 0) {
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    usize length = strlen(name);
    if (length >= VFS_NAME_MAX) {
        return false;
    }
    if (strchr(name, '/') != NULL) {
        return false;
    }
    return true;
}

static struct vfs_node *lookup_unlocked(struct vfs_node *base, const char *path) {
    if (path == NULL) {
        return NULL;
    }
    if (base == NULL) {
        base = root_node;
    }
    if (path[0] == '/') {
        base = root_node;
    }
    if (path[0] == 0) {
        return base;
    }
    char copy[VFS_PATH_MAX];
    usize length = strlen(path);
    if (length >= sizeof(copy)) {
        return NULL;
    }
    memcpy(copy, path, length + 1);
    char *cursor = copy;
    while (*cursor == '/') {
        cursor++;
    }
    struct vfs_node *node = base;
    while (*cursor != 0) {
        char *component = cursor;
        while (*cursor != 0 && *cursor != '/') {
            cursor++;
        }
        if (*cursor == '/') {
            *cursor++ = 0;
        }
        while (*cursor == '/') {
            cursor++;
        }
        if (component[0] == 0) {
            continue;
        }
        if (strcmp(component, ".") == 0) {
            continue;
        }
        if (strcmp(component, "..") == 0) {
            if (node->parent != NULL) {
                node = node->parent;
            }
            continue;
        }
        if (node->type != VFS_TYPE_DIRECTORY) {
            return NULL;
        }
        node = find_child(node, component);
        if (node == NULL) {
            return NULL;
        }
    }
    return node;
}

static int resolve_parent_unlocked(struct vfs_node *base, const char *path, char *leaf, usize leaf_size, struct vfs_node **parent_out) {
    if (path == NULL || path[0] == 0) {
        return VFS_ERROR_INVALID;
    }
    char copy[VFS_PATH_MAX];
    usize length = strlen(path);
    if (length >= sizeof(copy)) {
        return VFS_ERROR_INVALID;
    }
    memcpy(copy, path, length + 1);
    usize end = strlen(copy);
    while (end > 0 && copy[end - 1] == '/') {
        end--;
    }
    if (end == 0) {
        return VFS_ERROR_INVALID;
    }
    copy[end] = 0;
    char *last_slash = strrchr(copy, '/');
    struct vfs_node *parent = base;
    const char *leaf_name = copy;
    if (last_slash != NULL) {
        leaf_name = last_slash + 1;
        if (last_slash == copy) {
            parent = root_node;
        } else {
            *last_slash = 0;
            parent = lookup_unlocked(base, copy);
        }
    }
    if (!valid_name(leaf_name) || parent == NULL || parent->type != VFS_TYPE_DIRECTORY) {
        return VFS_ERROR_INVALID;
    }
    usize leaf_length = strlen(leaf_name);
    if (leaf_length + 1 > leaf_size) {
        return VFS_ERROR_INVALID;
    }
    memcpy(leaf, leaf_name, leaf_length + 1);
    *parent_out = parent;
    return VFS_OK;
}

static struct vfs_node *create_child_unlocked(struct vfs_node *parent, const char *name, u8 type, u32 device_id) {
    if (parent == NULL || parent->type != VFS_TYPE_DIRECTORY || find_child(parent, name) != NULL) {
        return NULL;
    }
    struct vfs_node *node = allocate_node();
    if (node == NULL) {
        return NULL;
    }
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = 0;
    node->type = type;
    node->device_id = device_id;
    node->parent = parent;
    node->next_sibling = parent->children;
    parent->children = node;
    return node;
}

static int ensure_capacity(struct vfs_node *node, u64 needed) {
    if (needed <= node->capacity) {
        return VFS_OK;
    }
    if (needed > VFS_MAX_FILE_SIZE) {
        return VFS_ERROR_NO_SPACE;
    }
    u64 capacity = node->capacity == 0 ? 256 : node->capacity;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity > VFS_MAX_FILE_SIZE) {
            capacity = VFS_MAX_FILE_SIZE;
        }
    }
    u8 *data = kmalloc((usize)capacity);
    if (data == NULL) {
        return VFS_ERROR_NO_SPACE;
    }
    memset(data, 0, (usize)capacity);
    if (node->data != NULL && node->size > 0) {
        memcpy(data, node->data, (usize)node->size);
        kfree(node->data);
    }
    node->data = data;
    node->capacity = capacity;
    return VFS_OK;
}

static int write_unlocked(struct vfs_node *node, u64 offset, const void *buffer, u64 length) {
    if (node == NULL || !node->used) {
        return VFS_ERROR_INVALID;
    }
    if (node->type == VFS_TYPE_DEVICE) {
        if (node->device_id == VFS_DEVICE_NULL) {
            return (int)length;
        }
        if (node->device_id == VFS_DEVICE_CONSOLE) {
            console_write_n((const char *)buffer, (usize)length);
            return (int)length;
        }
        return VFS_ERROR_TYPE;
    }
    if (node->type != VFS_TYPE_FILE) {
        return VFS_ERROR_TYPE;
    }
    if (offset + length < offset || offset + length > VFS_MAX_FILE_SIZE) {
        return VFS_ERROR_NO_SPACE;
    }
    int result = ensure_capacity(node, offset + length);
    if (result != VFS_OK) {
        return result;
    }
    if (offset > node->size) {
        memset(node->data + node->size, 0, (usize)(offset - node->size));
    }
    memcpy(node->data + offset, buffer, (usize)length);
    if (offset + length > node->size) {
        node->size = offset + length;
    }
    return (int)length;
}

static int create_path(struct vfs_node *base, const char *path, u8 type) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    char leaf[VFS_NAME_MAX];
    struct vfs_node *parent = NULL;
    int result = resolve_parent_unlocked(base, path, leaf, sizeof(leaf), &parent);
    if (result != VFS_OK) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return result;
    }
    if (find_child(parent, leaf) != NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_EXISTS;
    }
    if (create_child_unlocked(parent, leaf, type, 0) == NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_NO_SPACE;
    }
    spin_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

static void initialize_file(const char *path, const char *content) {
    struct vfs_node *node = lookup_unlocked(root_node, path);
    if (node == NULL) {
        if (create_path(root_node, path, VFS_TYPE_FILE) != VFS_OK) {
            return;
        }
        node = lookup_unlocked(root_node, path);
    }
    if (node != NULL) {
        write_unlocked(node, 0, content, strlen(content));
    }
}

void vfs_init(void) {
    memset(nodes, 0, sizeof(nodes));
    root_node = allocate_node();
    if (root_node == NULL) {
        panic("failed to create VFS root");
    }
    root_node->type = VFS_TYPE_DIRECTORY;
    strncpy(root_node->name, "/", sizeof(root_node->name) - 1);
    if (create_child_unlocked(root_node, "dev", VFS_TYPE_DIRECTORY, 0) == NULL ||
        create_child_unlocked(root_node, "etc", VFS_TYPE_DIRECTORY, 0) == NULL ||
        create_child_unlocked(root_node, "home", VFS_TYPE_DIRECTORY, 0) == NULL ||
        create_child_unlocked(root_node, "tmp", VFS_TYPE_DIRECTORY, 0) == NULL) {
        panic("failed to create base directories");
    }
    struct vfs_node *dev = find_child(root_node, "dev");
    create_child_unlocked(dev, "null", VFS_TYPE_DEVICE, VFS_DEVICE_NULL);
    create_child_unlocked(dev, "console", VFS_TYPE_DEVICE, VFS_DEVICE_CONSOLE);
    create_child_unlocked(dev, "random", VFS_TYPE_DEVICE, VFS_DEVICE_RANDOM);
    initialize_file("/README", "Z-OS is a small 64-bit operating system.\n\nThis image boots through a two-stage BIOS loader into a preemptive\nkernel with virtual memory, a kernel heap, ring-3 syscalls, a VFS\nand an in-memory filesystem.\n\nType help to list shell commands.\n");
    initialize_file("/etc/motd", "Welcome to Z-OS.\n");
    initialize_file("/etc/version", Z_OS_VERSION "\n");
    initialize_file("/home/welcome", "This home directory lives in ramfs.\n");
    kprintf("VFS: ramfs mounted at / with %u node slots\n", VFS_MAX_NODES);
}

struct vfs_node *vfs_root(void) {
    return root_node;
}

struct vfs_node *vfs_lookup(struct vfs_node *base, const char *path) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    struct vfs_node *node = lookup_unlocked(base, path);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return node;
}

int vfs_create(struct vfs_node *base, const char *path) {
    return create_path(base, path, VFS_TYPE_FILE);
}

int vfs_mkdir(struct vfs_node *base, const char *path) {
    return create_path(base, path, VFS_TYPE_DIRECTORY);
}

int vfs_remove(struct vfs_node *base, const char *path) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    char leaf[VFS_NAME_MAX];
    struct vfs_node *parent = NULL;
    int result = resolve_parent_unlocked(base, path, leaf, sizeof(leaf), &parent);
    if (result != VFS_OK) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return result;
    }
    struct vfs_node *node = find_child(parent, leaf);
    if (node == NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_NOT_FOUND;
    }
    if (node->type == VFS_TYPE_DIRECTORY && node->children != NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_NOT_EMPTY;
    }
    struct vfs_node **cursor = &parent->children;
    while (*cursor != NULL && *cursor != node) {
        cursor = &(*cursor)->next_sibling;
    }
    if (*cursor == node) {
        *cursor = node->next_sibling;
    }
    if (node->data != NULL) {
        kfree(node->data);
    }
    memset(node, 0, sizeof(struct vfs_node));
    spin_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

int vfs_rename(struct vfs_node *base, const char *old_path, const char *new_path) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    char old_leaf[VFS_NAME_MAX];
    char new_leaf[VFS_NAME_MAX];
    struct vfs_node *old_parent = NULL;
    struct vfs_node *new_parent = NULL;
    int result = resolve_parent_unlocked(base, old_path, old_leaf, sizeof(old_leaf), &old_parent);
    if (result != VFS_OK) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return result;
    }
    result = resolve_parent_unlocked(base, new_path, new_leaf, sizeof(new_leaf), &new_parent);
    if (result != VFS_OK) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return result;
    }
    struct vfs_node *node = find_child(old_parent, old_leaf);
    if (node == NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_NOT_FOUND;
    }
    if (find_child(new_parent, new_leaf) != NULL) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_EXISTS;
    }
    struct vfs_node *ancestor = new_parent;
    while (ancestor != NULL) {
        if (ancestor == node) {
            spin_unlock_irqrestore(&vfs_lock, flags);
            return VFS_ERROR_INVALID;
        }
        ancestor = ancestor->parent;
    }
    struct vfs_node **cursor = &old_parent->children;
    while (*cursor != NULL && *cursor != node) {
        cursor = &(*cursor)->next_sibling;
    }
    if (*cursor == node) {
        *cursor = node->next_sibling;
    }
    node->parent = new_parent;
    node->next_sibling = new_parent->children;
    new_parent->children = node;
    strncpy(node->name, new_leaf, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = 0;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

int vfs_read(struct vfs_node *node, u64 offset, void *buffer, u64 length) {
    if (node == NULL || !node->used) {
        return VFS_ERROR_INVALID;
    }
    if (node->type == VFS_TYPE_DEVICE) {
        if (node->device_id == VFS_DEVICE_NULL) {
            return 0;
        }
        if (node->device_id == VFS_DEVICE_CONSOLE) {
            return 0;
        }
        if (node->device_id == VFS_DEVICE_RANDOM) {
            random_bytes(buffer, (usize)length);
            return (int)length;
        }
        return VFS_ERROR_TYPE;
    }
    if (node->type != VFS_TYPE_FILE) {
        return VFS_ERROR_TYPE;
    }
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    if (offset >= node->size || length == 0) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return 0;
    }
    u64 available = node->size - offset;
    if (length > available) {
        length = available;
    }
    memcpy(buffer, node->data + offset, (usize)length);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return (int)length;
}

int vfs_write(struct vfs_node *node, u64 offset, const void *buffer, u64 length) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    int result = write_unlocked(node, offset, buffer, length);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return result;
}

int vfs_append(struct vfs_node *node, const void *buffer, u64 length) {
    if (node == NULL) {
        return VFS_ERROR_INVALID;
    }
    return vfs_write(node, node->size, buffer, length);
}

int vfs_truncate(struct vfs_node *node) {
    if (node == NULL || !node->used) {
        return VFS_ERROR_INVALID;
    }
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    if (node->type != VFS_TYPE_FILE) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return VFS_ERROR_TYPE;
    }
    if (node->data != NULL) {
        kfree(node->data);
        node->data = NULL;
    }
    node->size = 0;
    node->capacity = 0;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

u32 vfs_list(struct vfs_node *directory, struct vfs_dirent *buffer, u32 capacity) {
    if (directory == NULL || directory->type != VFS_TYPE_DIRECTORY) {
        return 0;
    }
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    u32 written = 0;
    struct vfs_node *child = directory->children;
    while (child != NULL && written < capacity) {
        strncpy(buffer[written].name, child->name, sizeof(buffer[written].name) - 1);
        buffer[written].name[sizeof(buffer[written].name) - 1] = 0;
        buffer[written].type = child->type;
        buffer[written].size = child->size;
        written++;
        child = child->next_sibling;
    }
    spin_unlock_irqrestore(&vfs_lock, flags);
    return written;
}

int vfs_stat(struct vfs_node *node, struct vfs_stat *stat) {
    if (node == NULL || stat == NULL || !node->used) {
        return VFS_ERROR_INVALID;
    }
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    stat->type = node->type;
    stat->size = node->size;
    stat->device_id = node->device_id;
    u32 count = 0;
    struct vfs_node *child = node->children;
    while (child != NULL) {
        count++;
        child = child->next_sibling;
    }
    stat->child_count = count;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return VFS_OK;
}

int vfs_build_path(struct vfs_node *node, char *buffer, usize size) {
    if (node == NULL || buffer == NULL || size == 0) {
        return VFS_ERROR_INVALID;
    }
    struct vfs_node *chain[VFS_MAX_NODES];
    u32 depth = 0;
    struct vfs_node *cursor = node;
    while (cursor != NULL && depth < VFS_MAX_NODES) {
        chain[depth++] = cursor;
        cursor = cursor->parent;
    }
    if (depth == 0) {
        return VFS_ERROR_INVALID;
    }
    usize position = 0;
    if (depth == 1) {
        if (size < 2) {
            return VFS_ERROR_NO_SPACE;
        }
        buffer[0] = '/';
        buffer[1] = 0;
        return VFS_OK;
    }
    for (u32 index = depth - 1; index > 0; index--) {
        usize length = strlen(chain[index]->name);
        if (position + length + 1 >= size) {
            return VFS_ERROR_NO_SPACE;
        }
        buffer[position++] = '/';
        memcpy(buffer + position, chain[index]->name, length);
        position += length;
    }
    if (position == 0) {
        if (size < 2) {
            return VFS_ERROR_NO_SPACE;
        }
        buffer[position++] = '/';
    }
    buffer[position] = 0;
    return VFS_OK;
}

const char *vfs_node_name(struct vfs_node *node) {
    return node != NULL ? node->name : "";
}

bool vfs_is_directory(struct vfs_node *node) {
    return node != NULL && node->type == VFS_TYPE_DIRECTORY;
}

bool vfs_is_file(struct vfs_node *node) {
    return node != NULL && node->type == VFS_TYPE_FILE;
}

struct vfs_stats vfs_get_stats(void) {
    u64 flags;
    spin_lock_irqsave(&vfs_lock, &flags);
    struct vfs_stats stats = {0};
    for (u32 index = 0; index < VFS_MAX_NODES; index++) {
        if (nodes[index].used) {
            stats.nodes_used++;
            stats.bytes_allocated += nodes[index].capacity;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, flags);
    return stats;
}
