#include "fat.h"
#include "ata.h"
#include "heap.h"
#include "serial.h"
#include "vfs.h"
#include <stddef.h>
#include "string.h"

#define FAT_EOC 0x0FFFFFF8

typedef struct {
    int bus;
    int master;
    fat_bpb_t bpb;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t bytes_per_cluster;
} fat_private_t;

static fat_private_t *fat_get_private(vfs_node_t *node) {
    return (fat_private_t *) node->fs->private;
}

static uint32_t fat_cluster_to_lba(fat_private_t *fp, uint32_t cluster) {
    return fp->data_start + (cluster - 2) * fp->bpb.sectors_per_cluster;
}

static uint32_t fat_next_cluster(fat_private_t *fp, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fp->fat_start + fat_offset / fp->bpb.bytes_per_sector;
    uint32_t ent_offset = fat_offset % fp->bpb.bytes_per_sector;

    uint8_t buf[512];
    if (ata_read_sectors(fp->bus, fp->master, fat_sector, 1, buf) < 0)
        return FAT_EOC;

    uint32_t val = *(uint32_t *)(buf + ent_offset) & 0x0FFFFFFF;
    return val;
}

static int fat_set_cluster(fat_private_t *fp, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fp->fat_start + fat_offset / fp->bpb.bytes_per_sector;
    uint32_t ent_offset = fat_offset % fp->bpb.bytes_per_sector;

    uint8_t buf[512];
    if (ata_read_sectors(fp->bus, fp->master, fat_sector, 1, buf) < 0)
        return -1;

    uint32_t *ent = (uint32_t *)(buf + ent_offset);
    *ent = (*ent & 0xF0000000) | (value & 0x0FFFFFFF);

    if (ata_write_sectors(fp->bus, fp->master, fat_sector, 1, buf) < 0)
        return -1;

    return 0;
}

static vfs_node_t *fat_create_node(const char *name, uint32_t size, uint8_t flags, uint32_t cluster, vfs_filesystem_t *fs, vfs_node_t *parent) {
    vfs_node_t *node = (vfs_node_t *) kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    strcpy(node->name, name);
    node->size = size;
    node->flags = flags;
    node->fs = fs;
    node->fs_data = (void *)(uintptr_t) cluster;
    node->parent = parent;
    node->children = NULL;
    node->next = NULL;
    return node;
}

static int fat_read(vfs_node_t *node, uint64_t offset, uint64_t size, void *buf) {
    fat_private_t *fp = fat_get_private(node);
    uint32_t cluster = (uint32_t)(uintptr_t) node->fs_data;

    uint32_t cluster_size = fp->bytes_per_cluster;
    uint8_t *out = (uint8_t *) buf;
    uint64_t done = 0;

    uint64_t skip = offset;
    while (skip >= cluster_size) {
        cluster = fat_next_cluster(fp, cluster);
        if (cluster >= FAT_EOC) return -1;
        skip -= cluster_size;
    }

    while (done < size) {
        uint32_t lba = fat_cluster_to_lba(fp, cluster);
        uint64_t cluster_off = skip;
        uint64_t to_read = cluster_size - cluster_off;
        if (to_read > size - done) to_read = size - done;
        uint64_t cluster_start = done;

        uint8_t tmp[512];
        uint32_t sector_off = (uint32_t)(cluster_off / 512);
        uint32_t byte_off = (uint32_t)(cluster_off % 512);

        for (uint64_t s = 0; s < (to_read + 511) / 512; s++) {
            if (ata_read_sectors(fp->bus, fp->master, lba + sector_off + s, 1, tmp) < 0)
                return -1;
            uint64_t cbytes = 512 - byte_off;
            uint64_t cluster_done = done - cluster_start;
            if (cbytes > to_read - cluster_done)
                cbytes = to_read - cluster_done;
            if (cbytes > 512) cbytes = 512;

            for (uint64_t i = 0; i < cbytes; i++)
                out[done++] = tmp[byte_off + i];
            byte_off = 0;

            if (done >= size) break;
        }

        skip = 0;
        if (done >= size) break;

        cluster = fat_next_cluster(fp, cluster);
        if (cluster >= FAT_EOC) break;
    }
    return (int) done;
}

static void fat_trim_name(char *out, const char *name, const char *ext) {
    int o = 0;
    for (int i = 0; i < 8 && name[i] && name[i] != ' '; i++)
        out[o++] = name[i];
    for (int i = 7; i >= 0 && name[i] == ' '; i--);
    if (ext[0] && ext[0] != ' ') {
        out[o++] = '.';
        for (int i = 0; i < 3 && ext[i] && ext[i] != ' '; i++)
            out[o++] = ext[i];
    }
    out[o] = 0;

    for (int i = 0; out[i]; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char) (out[i] - 'A' + 'a');
    }
}

static uint32_t fat_total_clusters(fat_private_t *fp) {
    uint32_t total_sectors = fp->bpb.total_sectors_32;
    if (total_sectors == 0)
        total_sectors = fp->bpb.total_sectors_16;
    if (total_sectors == 0)
        total_sectors = 8192;
    uint32_t data_sectors = total_sectors - fp->data_start;
    return data_sectors / fp->bpb.sectors_per_cluster;
}

static uint32_t fat_alloc_cluster(fat_private_t *fp) {
    uint32_t total = fat_total_clusters(fp);
    uint8_t buf[512];
    for (uint32_t c = 2; c < total + 2; c++) {
        uint32_t fat_offset = c * 4;
        uint32_t fat_sector = fp->fat_start + fat_offset / 512;
        uint32_t ent_off = fat_offset % 512;
        if (ent_off == 0) {
            if (ata_read_sectors(fp->bus, fp->master, fat_sector, 1, buf) < 0)
                return 0;
        }
        uint32_t val = *(uint32_t *)(buf + ent_off) & 0x0FFFFFFF;
        if (val == 0) {
            fat_set_cluster(fp, c, FAT_EOC);
            return c;
        }
    }
    return 0;
}

static int fat_write(vfs_node_t *node, uint64_t offset, uint64_t size, void *buf) {
    fat_private_t *fp = fat_get_private(node);
    uint32_t cluster = (uint32_t)(uintptr_t) node->fs_data;
    uint32_t cluster_size = fp->bytes_per_cluster;
    uint8_t *in = (uint8_t *) buf;
    uint64_t done = 0;

    if (size == 0) return 0;

    uint64_t skip = offset;
    while (skip >= cluster_size) {
        uint32_t next = fat_next_cluster(fp, cluster);
        if (next >= FAT_EOC || next == 0) {
            next = fat_alloc_cluster(fp);
            if (!next) return (int)done;
            fat_set_cluster(fp, cluster, next);
        }
        cluster = next;
        skip -= cluster_size;
    }

    while (done < size) {
        uint32_t lba = fat_cluster_to_lba(fp, cluster);
        uint64_t cluster_off = skip;
        uint64_t to_write = cluster_size - cluster_off;
        if (to_write > size - done) to_write = size - done;

        uint8_t tmp[512];
        uint32_t sector_off = (uint32_t)(cluster_off / 512);
        uint32_t byte_off = (uint32_t)(cluster_off % 512);

        for (uint64_t s = 0; s < (to_write + 511) / 512; s++) {
            uint32_t cur_lba = lba + sector_off + s;
            uint64_t cbytes = 512 - byte_off;
            if (cbytes > size - done) cbytes = size - done;

            if (byte_off > 0 || cbytes < 512) {
                ata_read_sectors(fp->bus, fp->master, cur_lba, 1, tmp);
            }

            for (uint64_t i = 0; i < cbytes; i++)
                tmp[byte_off + i] = in[done++];

            byte_off = 0;
            ata_write_sectors(fp->bus, fp->master, cur_lba, 1, tmp);
            if (done >= size) break;
        }

        skip = 0;
        if (done >= size) break;

        uint32_t next = fat_next_cluster(fp, cluster);
        if (next >= FAT_EOC || next == 0) {
            next = fat_alloc_cluster(fp);
            if (!next) return (int)done;
            fat_set_cluster(fp, cluster, next);
        }
        cluster = next;
    }

    if (offset + done > node->size)
        node->size = (uint32_t)(offset + done);

    return (int)done;
}

static int fat_readdir(vfs_node_t *node, int (*cb)(const char *, uint32_t, uint8_t, void *), void *arg) {
    fat_private_t *fp = fat_get_private(node);
    uint32_t cluster = (uint32_t)(uintptr_t) node->fs_data;

    uint8_t buf[512];
    int entry_count = 0;

    while (cluster < FAT_EOC) {
        uint32_t lba = fat_cluster_to_lba(fp, cluster);
        uint32_t entries_per_cluster = fp->bytes_per_cluster / 32;

        for (uint32_t e = 0; e < entries_per_cluster; e++) {
            uint32_t sector = (e * 32) / 512;
            uint32_t off = (e * 32) % 512;

            if (sector == 0 || off == 0) {
                if (ata_read_sectors(fp->bus, fp->master, lba + sector, 1, buf) < 0)
                    return -1;
            }

            fat_dirent_t *de = (fat_dirent_t *)(buf + off);

            if (de->name[0] == 0)
                goto done;

            if ((uint8_t) de->name[0] == 0xE5)
                continue;

            if (de->attrs == FAT_ATTR_LFN)
                continue;

            if (de->attrs & FAT_ATTR_VOLUME)
                continue;

            char name[256];
            fat_trim_name(name, de->name, de->ext);

            uint32_t de_cluster = ((uint32_t) de->cluster_hi << 16) | de->cluster_lo;

            uint8_t flags = (de->attrs & FAT_ATTR_DIR) ? VFS_DIR : VFS_FILE;

            vfs_node_t *child = fat_create_node(name, de->size, flags, de_cluster, node->fs, node);
            if (!child) continue;

            child->next = node->children;
            node->children = child;

            if (cb) cb(name, de->size, flags, arg);
            entry_count++;
        }
        cluster = fat_next_cluster(fp, cluster);
    }
done:
    return entry_count;
}

vfs_filesystem_t *fat_mount(int bus, int master, int partition) {
    (void) partition;

    serial_print("[fat] mounting bus=");
    serial_print_dec(bus);
    serial_print(" master=");
    serial_print_dec(master);
    serial_print("\n");

    fat_private_t *fp = (fat_private_t *) kmalloc(sizeof(fat_private_t));
    if (!fp) return NULL;

    fp->bus = bus;
    fp->master = master;

    uint8_t buf[512];
    if (ata_read_sectors(bus, master, 0, 1, buf) < 0) {
        kfree(fp);
        return NULL;
    }

    fat_bpb_t *bpb = (fat_bpb_t *) buf;
    fp->bpb = *bpb;

    if (bpb->bytes_per_sector != 512) {
        serial_print("[fat] unsupported sector size\n");
        kfree(fp);
        return NULL;
    }

    if (bpb->sig != 0x29 && bpb->sig != 0x28) {
        serial_print("[fat] no FAT signature\n");
        kfree(fp);
        return NULL;
    }

    if (bpb->fat_size_32 == 0) {
        serial_print("[fat] not FAT32\n");
        kfree(fp);
        return NULL;
    }

    fp->sectors_per_fat = bpb->fat_size_32;
    fp->fat_start = bpb->reserved_sectors;
    fp->root_cluster = bpb->root_cluster;
    fp->bytes_per_cluster = bpb->bytes_per_sector * bpb->sectors_per_cluster;

    uint32_t root_sectors = ((bpb->root_max_entries * 32) + bpb->bytes_per_sector - 1) / bpb->bytes_per_sector;
    fp->data_start = fp->fat_start + bpb->fat_count * fp->sectors_per_fat + root_sectors;

    serial_print("[fat] BPB: sectors/cluster=");
    serial_print_dec(bpb->sectors_per_cluster);
    serial_print(" reserved=");
    serial_print_dec(bpb->reserved_sectors);
    serial_print(" fats=");
    serial_print_dec(bpb->fat_count);
    serial_print(" root_cluster=");
    serial_print_dec(bpb->root_cluster);
    serial_print("\n");

    vfs_filesystem_t *fs = (vfs_filesystem_t *) kmalloc(sizeof(vfs_filesystem_t));
    if (!fs) {
        kfree(fp);
        return NULL;
    }
    strcpy(fs->name, "fat32");
    fs->read = fat_read;
    fs->write = fat_write;
    fs->readdir = fat_readdir;

    serial_print("[fat] creating root node...\n");
    fs->root = fat_create_node("/", 0, VFS_DIR, fp->root_cluster, fs, NULL);
    fs->private = fp;

    serial_print("[fat] mounted OK\n");
    return fs;
}
