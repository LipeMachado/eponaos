#include "vfs.h"
#include "heap.h"
#include "serial.h"
#include <stddef.h>
#include "string.h"

#define VFS_MAX_MOUNTS 16

static vfs_filesystem_t *g_mounts[VFS_MAX_MOUNTS];
static char g_mount_paths[VFS_MAX_MOUNTS][256];
static int g_mount_count = 0;

void vfs_init(void) {
    serial_print("[vfs] init\n");
    g_mount_count = 0;
}

int vfs_mount(const char *path, vfs_filesystem_t *fs) {
    for (int i = 0; i < g_mount_count; i++) {
        if (strcmp(g_mount_paths[i], path) == 0)
            return -1;
    }
    if (g_mount_count >= VFS_MAX_MOUNTS)
        return -1;

    strcpy(g_mount_paths[g_mount_count], path);
    g_mounts[g_mount_count] = fs;
    g_mount_count++;
    return 0;
}

static int vfs_split_path(const char *full, char *mount, char *rest) {
    mount[0] = 0;
    rest[0] = 0;
    if (full[0] != '/')
        return -1;

    int best = -1;
    size_t best_len = 0;

    for (int i = 0; i < g_mount_count; i++) {
        size_t len = strlen(g_mount_paths[i]);
        if (strncmp(g_mount_paths[i], full, len) == 0) {
            if (len > best_len) {
                best = i;
                best_len = len;
            }
        }
    }

    if (best < 0)
        return -1;

    strcpy(mount, g_mount_paths[best]);
    if (full[best_len] == '/')
        strcpy(rest, full + best_len + 1);
    else if (full[best_len] == 0)
        rest[0] = 0;
    else
        strcpy(rest, full + best_len);

    return best;
}

static vfs_node_t *vfs_resolve(vfs_filesystem_t *fs, const char *path) {
    if (!path || path[0] == 0)
        return fs->root;

    vfs_node_t *curr = fs->root;
    char buf[256];
    strcpy(buf, path);
    char *part = strtok(buf, "/");

    while (part) {
        if (!curr || !(curr->flags & VFS_DIR))
            return NULL;

        if (!curr->children && fs->readdir) {
            fs->readdir(curr, NULL, NULL);
        }

        vfs_node_t *child = curr->children;
        while (child) {
            if (strcmp(child->name, part) == 0)
                break;
            child = child->next;
        }
        if (!child)
            return NULL;
        curr = child;
        part = strtok(NULL, "/");
    }
    return curr;
}

static int vfs_split_parent(const char *path, char *parent, char *name) {
    size_t len = strlen(path);
    if (len == 0) return -1;

    size_t slash = len;
    while (slash > 0 && path[slash - 1] != '/')
        slash--;

    if (slash == len) return -1;

    if (slash == 0) {
        parent[0] = 0;
    } else {
        size_t plen = slash - 1;
        if (plen > 255) plen = 255;
        memcpy(parent, path, plen);
        parent[plen] = 0;
    }

    strcpy(name, path + slash);
    return name[0] ? 0 : -1;
}

file_t *vfs_open(const char *path) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return NULL;

    vfs_filesystem_t *fs = g_mounts[idx];
    vfs_node_t *node = vfs_resolve(fs, rest);
    if (!node || !(node->flags & VFS_FILE))
        return NULL;

    file_t *f = (file_t *) kmalloc(sizeof(file_t));
    if (!f) return NULL;
    f->node = node;
    f->offset = 0;
    return f;
}

file_t *vfs_create(const char *path) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return NULL;

    vfs_filesystem_t *fs = g_mounts[idx];
    if (!fs->create) return NULL;

    vfs_node_t *existing = vfs_resolve(fs, rest);
    if (existing && (existing->flags & VFS_FILE)) {
        existing->size = 0;
        file_t *f = (file_t *) kmalloc(sizeof(file_t));
        if (!f) return NULL;
        f->node = existing;
        f->offset = 0;
        return f;
    }

    char parent_path[256], name[256];
    if (vfs_split_parent(rest, parent_path, name) < 0)
        return NULL;

    vfs_node_t *parent = vfs_resolve(fs, parent_path);
    if (!parent || !(parent->flags & VFS_DIR))
        return NULL;

    if (!parent->children && fs->readdir)
        fs->readdir(parent, NULL, NULL);

    vfs_node_t *node = fs->create(parent, name, VFS_FILE);
    if (!node) return NULL;

    file_t *f = (file_t *) kmalloc(sizeof(file_t));
    if (!f) return NULL;
    f->node = node;
    f->offset = 0;
    return f;
}

int vfs_mkdir(const char *path) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return -1;

    vfs_filesystem_t *fs = g_mounts[idx];
    if (!fs->create) return -1;

    if (vfs_resolve(fs, rest) != NULL)
        return -1; /* ja existe */

    char parent_path[256], name[256];
    if (vfs_split_parent(rest, parent_path, name) < 0)
        return -1;

    vfs_node_t *parent = vfs_resolve(fs, parent_path);
    if (!parent || !(parent->flags & VFS_DIR))
        return -1;

    if (!parent->children && fs->readdir)
        fs->readdir(parent, NULL, NULL);

    vfs_node_t *node = fs->create(parent, name, VFS_DIR);
    return node ? 0 : -1;
}

int vfs_read(file_t *f, uint64_t size, void *buf) {
    if (!f || !f->node || !f->node->fs || !f->node->fs->read)
        return -1;
    uint64_t remain = f->node->size - f->offset;
    if (size > remain) size = remain;
    int ret = f->node->fs->read(f->node, f->offset, size, buf);
    if (ret > 0) {
        if ((uint64_t)ret > size) ret = (int)size;
        f->offset += ret;
    }
    return ret;
}

int vfs_write(file_t *f, uint64_t size, void *buf) {
    if (!f || !f->node || !f->node->fs || !f->node->fs->write)
        return -1;
    int ret = f->node->fs->write(f->node, f->offset, size, buf);
    if (ret > 0)
        f->offset += ret;
    return ret;
}

void vfs_close(file_t *f) {
    if (f) kfree(f);
}

int vfs_unlink(const char *path) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return -1;

    vfs_filesystem_t *fs = g_mounts[idx];
    if (!fs->unlink) return -1;

    char parent_path[256], name[256];
    if (vfs_split_parent(rest, parent_path, name) < 0)
        return -1;

    vfs_node_t *parent = vfs_resolve(fs, parent_path);
    if (!parent || !(parent->flags & VFS_DIR))
        return -1;

    if (!parent->children && fs->readdir)
        fs->readdir(parent, NULL, NULL);

    vfs_node_t *prev = NULL;
    vfs_node_t *child = parent->children;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            int ret = fs->unlink(parent, name);
            if (ret < 0) return ret;
            if (prev)
                prev->next = child->next;
            else
                parent->children = child->next;
            return 0;
        }
        prev = child;
        child = child->next;
    }

    return fs->unlink(parent, name);
}

int vfs_stat(const char *path, uint32_t *size, uint8_t *flags) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return -1;

    vfs_filesystem_t *fs = g_mounts[idx];
    vfs_node_t *node = vfs_resolve(fs, rest);
    if (!node) return -1;

    if (size) *size = node->size;
    if (flags) *flags = node->flags;
    return 0;
}

int vfs_readdir(const char *path, int (*cb)(const char *, uint32_t, uint8_t, void *), void *arg) {
    char mount[256], rest[256];
    int idx = vfs_split_path(path, mount, rest);
    if (idx < 0) return -1;

    vfs_filesystem_t *fs = g_mounts[idx];
    vfs_node_t *node = vfs_resolve(fs, rest);
    if (!node || !(node->flags & VFS_DIR))
        return -1;

    if (!node->children && fs->readdir)
        fs->readdir(node, NULL, NULL);

    vfs_node_t *child = node->children;
    while (child) {
        if (cb) cb(child->name, child->size, child->flags, arg);
        child = child->next;
    }
    return 0;
}
