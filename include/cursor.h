#ifndef EPONA_CURSOR_H
#define EPONA_CURSOR_H

#include <stdint.h>

#define CURSOR_MAX_W 64
#define CURSOR_MAX_H 64

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
    uint32_t pixels[CURSOR_MAX_W * CURSOR_MAX_H];
} cursor_t;

int cursor_load(const char *path, cursor_t *out);
void cursor_render(const cursor_t *cur, int x, int y);

#endif
