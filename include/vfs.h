#ifndef EPONA_VFS_H
#define EPONA_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_FILE      1
#define VFS_DIR       2
#define VFS_MOUNTPOINT 4

typedef struct vfs_node vfs_node_t;
typedef struct vfs_filesystem vfs_filesystem_t;

struct vfs_node {
    char name[256];
    uint32_t size;
    uint8_t flags;
    vfs_filesystem_t *fs;
    void *fs_data;
    vfs_node_t *parent;
    vfs_node_t *children;
    vfs_node_t *next;
};

struct vfs_filesystem {
    char name[32];
    vfs_node_t *root;
    void *private;
    int (*read)(vfs_node_t *node, uint64_t offset, uint64_t size, void *buf);
    int (*readdir)(vfs_node_t *node, int (*cb)(const char *name, uint32_t size, uint8_t flags, void *), void *arg);
};

typedef struct {
    vfs_node_t *node;
    uint64_t offset;
} file_t;

void vfs_init(void);
int vfs_mount(const char *path, vfs_filesystem_t *fs);
file_t *vfs_open(const char *path);
int vfs_read(file_t *f, uint64_t size, void *buf);
void vfs_close(file_t *f);
int vfs_readdir(const char *path, int (*cb)(const char *name, uint32_t size, uint8_t flags, void *), void *arg);

#endif
