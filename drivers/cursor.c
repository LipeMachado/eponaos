#include "cursor.h"
#include "vfs.h"
#include "heap.h"
#include "gpu.h"
#include "string.h"
#include <stddef.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t reserved;
    uint16_t type;
    uint16_t count;
} ico_header_t;

typedef struct {
    uint8_t  width;
    uint8_t  height;
    uint8_t  color_count;
    uint8_t  reserved;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
    uint32_t data_size;
    uint32_t data_offset;
} ico_entry_t;

typedef struct {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    uint32_t x_pixels_per_meter;
    uint32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;
#pragma pack(pop)

int cursor_load(const char *path, cursor_t *out) {
    if (!out) return -1;

    file_t *f = vfs_open(path);
    if (!f) return -1;

    ico_header_t ico;
    if (vfs_read(f, sizeof(ico), &ico) < (int)sizeof(ico)) {
        vfs_close(f);
        return -1;
    }

    if (ico.type != 2 || ico.count == 0) {
        vfs_close(f);
        return -1;
    }

    ico_entry_t entry;
    if (vfs_read(f, sizeof(entry), &entry) < (int)sizeof(entry)) {
        vfs_close(f);
        return -1;
    }

    out->hotspot_x = entry.hotspot_x;
    out->hotspot_y = entry.hotspot_y;

    f->offset = entry.data_offset;

    bmp_info_header_t bmp;
    if (vfs_read(f, sizeof(bmp), &bmp) < (int)sizeof(bmp)) {
        vfs_close(f);
        return -1;
    }

    uint32_t w = bmp.width;
    uint32_t h = bmp.height / 2;

    if (w > CURSOR_MAX_W || h > CURSOR_MAX_H || w == 0 || h == 0) {
        vfs_close(f);
        return -1;
    }

    out->width = (uint16_t)w;
    out->height = (uint16_t)h;

    uint32_t row_size = ((w * bmp.bpp + 31) / 32) * 4;
    uint32_t xor_size = row_size * h;

    uint8_t *xor_data = (uint8_t *)kmalloc(xor_size);
    if (!xor_data) {
        vfs_close(f);
        return -1;
    }

    if (vfs_read(f, xor_size, xor_data) < (int)xor_size) {
        kfree(xor_data);
        vfs_close(f);
        return -1;
    }

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t src_y = h - 1 - y;
            uint32_t b = xor_data[src_y * row_size + x * 4 + 0];
            uint32_t g = xor_data[src_y * row_size + x * 4 + 1];
            uint32_t r = xor_data[src_y * row_size + x * 4 + 2];
            uint32_t a = xor_data[src_y * row_size + x * 4 + 3];
            out->pixels[y * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    kfree(xor_data);
    vfs_close(f);
    return 0;
}

void cursor_render(const cursor_t *cur, int x, int y) {
    for (uint16_t py = 0; py < cur->height; py++) {
        for (uint16_t px = 0; px < cur->width; px++) {
            uint32_t argb = cur->pixels[py * cur->width + px];
            uint8_t a = (uint8_t)(argb >> 24);
            if (a < 200) continue;

            uint32_t r = (argb >> 16) & 0xFF;
            uint32_t g = (argb >> 8) & 0xFF;
            uint32_t b = argb & 0xFF;

            uint32_t bg = gpu_get_pixel((uint16_t)(x + px), (uint16_t)(y + py));
            uint32_t br = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bb = bg & 0xFF;

            uint32_t dr = (br * (255 - a) + r * a) / 255;
            uint32_t dg = (bg_g * (255 - a) + g * a) / 255;
            uint32_t db = (bb * (255 - a) + b * a) / 255;
            gpu_put_pixel((uint16_t)(x + px), (uint16_t)(y + py), (dr << 16) | (dg << 8) | db);
        }
    }
}
