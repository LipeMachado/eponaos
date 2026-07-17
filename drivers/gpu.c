#include "gpu.h"
#include "io.h"
#include "paging.h"
#include "serial.h"
#include "heap.h"
#include <stddef.h>
#include <stdint.h>

#define VGA_MEM  ((volatile uint16_t *) 0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define VBE_MODE_INFO ((volatile vbe_mode_info_t *)0xF000)
#define VGA_FONT_8X16 ((const uint8_t *)0xE000)
#define FONT_W 8
#define FONT_H 16

typedef struct {
    uint16_t attributes;
    uint8_t win_a;
    uint8_t win_b;
    uint16_t granularity;
    uint16_t win_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
} __attribute__((packed)) vbe_mode_info_t;

static size_t g_row;
static size_t g_col;
static uint8_t g_color = 0x0F;
static int g_fb_enabled;
static volatile uint8_t *g_fb;
static uint16_t g_fb_width;
static uint16_t g_fb_height;
static uint16_t g_fb_pitch;
static uint8_t g_fb_bpp;
static volatile uint8_t *g_fb_back;
static int g_target_back;
static size_t g_fb_cols;
static size_t g_fb_rows;

/* "render target" alternativo: quando ativo (via gpu_begin_target), todo
 * desenho (pixels, texto, console) vai para este buffer em vez da tela real.
 * Usado pelas janelas do window manager para terem seu proprio conteudo,
 * que o compositor depois copia (gpu_blit_buffer) para a posicao na tela. */
static volatile uint8_t *g_render_target;
static uint16_t g_render_pitch, g_render_w, g_render_h;
static uint8_t g_render_bpp;

/* viewport do console (em celulas de texto) dentro do buffer/tela ativo -
 * por padrao cobre a tela inteira; uma janela usa gpu_set_console_viewport
 * para confinar seu proprio texto ao tamanho do seu buffer. */
static uint16_t g_vp_x, g_vp_y;
static uint16_t g_vp_cols, g_vp_rows;

static const uint32_t g_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static inline uint16_t fb_cell(char c, uint8_t color) {
    return (uint16_t) (uint8_t) c | ((uint16_t) color << 8);
}

static void fb_blit_row(volatile uint8_t *dst, const volatile uint8_t *src, size_t bytes);

static uint16_t rt_pitch(void) { return g_render_target ? g_render_pitch : g_fb_pitch; }
static uint16_t rt_width(void) { return g_render_target ? g_render_w : g_fb_width; }
static uint16_t rt_height(void) { return g_render_target ? g_render_h : g_fb_height; }
static uint8_t rt_bpp(void) { return g_render_target ? g_render_bpp : g_fb_bpp; }

/* move o cursor piscante do hardware p/ (g_row, g_col) */
static void fb_update_cursor(void) {
    if (g_fb_enabled) return;
    uint16_t pos = (uint16_t) (g_row * VGA_COLS + g_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

static uint32_t rgb_for_color(uint8_t color) {
    return g_palette[color & 0x0F];
}

static volatile uint8_t *fb_target(void) {
    if (g_render_target) return g_render_target;
    return (g_target_back && g_fb_back) ? g_fb_back : g_fb;
}

static void fb_put_pixel(uint16_t x, uint16_t y, uint32_t rgb) {
    if (x >= rt_width() || y >= rt_height()) return;

    volatile uint8_t *t = fb_target();
    uint8_t bpp = rt_bpp();
    volatile uint8_t *p = t + (uint32_t)y * rt_pitch() + (uint32_t)x * (bpp / 8);
    if (bpp == 32) {
        p[0] = (uint8_t)(rgb & 0xFF);
        p[1] = (uint8_t)((rgb >> 8) & 0xFF);
        p[2] = (uint8_t)((rgb >> 16) & 0xFF);
        p[3] = 0;
    } else if (bpp == 24) {
        p[0] = (uint8_t)(rgb & 0xFF);
        p[1] = (uint8_t)((rgb >> 8) & 0xFF);
        p[2] = (uint8_t)((rgb >> 16) & 0xFF);
    } else if (bpp == 16) {
        uint16_t r = (uint16_t)((rgb >> 19) & 0x1F);
        uint16_t g = (uint16_t)((rgb >> 10) & 0x3F);
        uint16_t b = (uint16_t)((rgb >> 3) & 0x1F);
        uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)(v >> 8);
    }
}

static uint32_t fb_get_pixel(uint16_t x, uint16_t y) {
    if (x >= rt_width() || y >= rt_height()) return 0;

    volatile uint8_t *t = fb_target();
    uint8_t bpp = rt_bpp();
    volatile uint8_t *p = t + (uint32_t)y * rt_pitch() + (uint32_t)x * (bpp / 8);
    if (bpp == 32 || bpp == 24)
        return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
    if (bpp == 16) {
        uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint32_t r = ((v >> 11) & 0x1F) << 3;
        uint32_t g = ((v >> 5) & 0x3F) << 2;
        uint32_t b = (v & 0x1F) << 3;
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}

static void fb_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb) {
    for (uint16_t yy = 0; yy < h; yy++) {
        for (uint16_t xx = 0; xx < w; xx++)
            fb_put_pixel((uint16_t)(x + xx), (uint16_t)(y + yy), rgb);
    }
}

static void fb_draw_char(size_t row, size_t col, char ch, uint8_t fg, uint8_t bg) {
    uint16_t x = (uint16_t)(g_vp_x + col * FONT_W);
    uint16_t y = (uint16_t)(g_vp_y + row * FONT_H);
    const uint8_t *glyph = VGA_FONT_8X16 + ((uint8_t)ch * FONT_H);
    uint32_t fg_rgb = rgb_for_color(fg);
    uint32_t bg_rgb = rgb_for_color(bg);

    for (uint16_t gy = 0; gy < FONT_H; gy++) {
        uint8_t bits = glyph[gy];
        for (uint16_t gx = 0; gx < FONT_W; gx++) {
            uint32_t rgb = (bits & (0x80 >> gx)) ? fg_rgb : bg_rgb;
            fb_put_pixel((uint16_t)(x + gx), (uint16_t)(y + gy), rgb);
        }
    }
}

static void fb_draw_char_px(uint16_t x, uint16_t y, char ch, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = VGA_FONT_8X16 + ((uint8_t)ch * FONT_H);

    for (uint16_t gy = 0; gy < FONT_H; gy++) {
        uint8_t bits = glyph[gy];
        for (uint16_t gx = 0; gx < FONT_W; gx++) {
            uint32_t rgb = (bits & (0x80 >> gx)) ? fg : bg;
            fb_put_pixel((uint16_t)(x + gx), (uint16_t)(y + gy), rgb);
        }
    }
}

static void fb_scroll(void) {
    uint16_t vp_w_px = (uint16_t)(g_vp_cols * FONT_W);
    uint16_t vp_h_px = (uint16_t)(g_vp_rows * FONT_H);
    size_t bpp_bytes = rt_bpp() / 8;
    size_t row_bytes = (size_t)vp_w_px * bpp_bytes;
    uint16_t pitch = rt_pitch();
    volatile uint8_t *t = fb_target();

    for (uint16_t row = 0; row + FONT_H < vp_h_px; row++) {
        size_t dst_off = (size_t)(g_vp_y + row) * pitch + (size_t)g_vp_x * bpp_bytes;
        size_t src_off = (size_t)(g_vp_y + row + FONT_H) * pitch + (size_t)g_vp_x * bpp_bytes;
        fb_blit_row(t + dst_off, t + src_off, row_bytes);
    }

    uint32_t bg = rgb_for_color((uint8_t)(g_color >> 4));
    fb_fill_rect(g_vp_x, (uint16_t)(g_vp_y + vp_h_px - FONT_H), vp_w_px, FONT_H, bg);
    g_row = g_vp_rows - 1;
}

static void fb_init_from_vbe(void) {
    const volatile vbe_mode_info_t *info = VBE_MODE_INFO;
    uint64_t fb_phys;
    uint64_t map_base;
    uint64_t map_bytes;
    size_t pages;

    if (!(info->attributes & 0x80) || !info->framebuffer)
        return;
    if (info->width < FONT_W || info->height < FONT_H)
        return;
    if (info->bpp != 16 && info->bpp != 24 && info->bpp != 32)
        return;

    fb_phys = info->framebuffer;
    map_base = fb_phys & ~(uint64_t)(PAGE_SIZE - 1);
    map_bytes = ((uint64_t)info->pitch * info->height) + (fb_phys - map_base);
    pages = (size_t)((map_bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (paging_map_range(map_base, map_base, pages, PAGE_RW) < 0)
        return;
    paging_flush_tlb();

    g_fb = (volatile uint8_t *)(uintptr_t)info->framebuffer;
    g_fb_width = info->width;
    g_fb_height = info->height;
    g_fb_pitch = info->pitch;
    g_fb_bpp = info->bpp;
    g_fb_cols = g_fb_width / FONT_W;
    g_fb_rows = g_fb_height / FONT_H;
    g_vp_x = 0;
    g_vp_y = 0;
    g_vp_cols = g_fb_cols;
    g_vp_rows = g_fb_rows;
    g_fb_enabled = 1;
}

void gpu_set_color(uint8_t fg, uint8_t bg) {
    g_color = (uint8_t) (fg | (bg << 4));
}

void gpu_init(void) {
    g_row = 0;
    g_col = 0;
    g_color = 0x0F;
    gpu_clear(0x0F, 0x00);
}

void gpu_init_framebuffer(void) {
    if (g_fb_enabled) return;

    fb_init_from_vbe();
    if (g_fb_enabled) {
        serial_print("[gpu] framebuffer enabled ");
        serial_print_dec(g_fb_width);
        serial_print("x");
        serial_print_dec(g_fb_height);
        serial_print("x");
        serial_print_dec(g_fb_bpp);
        serial_print("\n");
        gpu_clear(0x0F, 0x00);

        size_t fb_size = (size_t)g_fb_pitch * g_fb_height;
        g_fb_back = (volatile uint8_t *)kmalloc((uint32_t)fb_size);
        if (g_fb_back) {
            for (size_t i = 0; i < fb_size; i++)
                g_fb_back[i] = g_fb[i];
            serial_print("[gpu] back buffer allocated\n");
        } else {
            serial_print("[gpu] back buffer FAILED\n");
        }
    } else {
        serial_print("[gpu] framebuffer unavailable, using VGA text fallback\n");
    }
}

void gpu_clear(uint8_t fg, uint8_t bg) {
    g_row = 0;
    g_col = 0;
    g_color = (uint8_t) (fg | (bg << 4));
    if (g_fb_enabled) {
        fb_fill_rect(0, 0, g_fb_width, g_fb_height, rgb_for_color(bg));
    } else {
        for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
            VGA_MEM[i] = fb_cell(' ', g_color);
        fb_update_cursor();
    }
}

void gpu_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (g_fb_enabled) {
        if (row >= g_fb_rows || col >= g_fb_cols) return;
        fb_draw_char(row, col, c, fg, bg);
        return;
    }
    if (row >= VGA_ROWS || col >= VGA_COLS)
        return;
    VGA_MEM[row * VGA_COLS + col] = fb_cell(c, (uint8_t) (fg | (bg << 4)));
}

void gpu_write_at(uint8_t row, uint8_t col, const char *s, uint8_t fg, uint8_t bg) {
    for (size_t i = 0; s[i] && col + i < VGA_COLS; i++)
        gpu_put_at(row, (uint8_t) (col + i), s[i], fg, bg);
}

/* sobe todas as linhas 1 posicao e limpa a ultima */
static void console_scroll(void) {
    if (g_fb_enabled) {
        fb_scroll();
        return;
    }
    for (size_t i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
    for (size_t i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEM[i] = fb_cell(' ', g_color);
    g_row = VGA_ROWS - 1;
}

void gpu_putc(char c) {
    size_t cols = g_fb_enabled ? g_vp_cols : VGA_COLS;
    size_t rows = g_fb_enabled ? g_vp_rows : VGA_ROWS;

    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) g_col--;
    } else if (c == '\f') {
        gpu_clear(0x0F, 0x00);
        return;
    } else {
        if (g_fb_enabled) {
            fb_draw_char(g_row, g_col, c, (uint8_t)(g_color & 0x0F), (uint8_t)(g_color >> 4));
        } else {
            VGA_MEM[g_row * VGA_COLS + g_col] = fb_cell(c, g_color);
        }
        if (++g_col >= cols) {
            g_col = 0;
            g_row++;
        }
    }
    if (g_row >= rows)
        console_scroll();
    fb_update_cursor();
}

void gpu_print(const char *s) {
    for (size_t i = 0; s[i]; i++)
        gpu_putc(s[i]);
}

int gpu_is_framebuffer_enabled(void) {
    return g_fb_enabled;
}

uint16_t gpu_width(void) {
    return g_fb_enabled ? g_fb_width : VGA_COLS;
}

uint16_t gpu_height(void) {
    return g_fb_enabled ? g_fb_height : VGA_ROWS;
}

uint8_t gpu_bpp(void) {
    return g_fb_enabled ? g_fb_bpp : 0;
}

void gpu_put_pixel(uint16_t x, uint16_t y, uint32_t rgb) {
    if (!g_fb_enabled) return;
    fb_put_pixel(x, y, rgb);
}

uint32_t gpu_get_pixel(uint16_t x, uint16_t y) {
    if (!g_fb_enabled) return 0;
    return fb_get_pixel(x, y);
}

void gpu_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb) {
    if (!g_fb_enabled) return;
    fb_fill_rect(x, y, w, h, rgb);
}

void gpu_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb) {
    if (!g_fb_enabled || w == 0 || h == 0) return;

    for (uint16_t xx = 0; xx < w; xx++) {
        fb_put_pixel((uint16_t)(x + xx), y, rgb);
        fb_put_pixel((uint16_t)(x + xx), (uint16_t)(y + h - 1), rgb);
    }
    for (uint16_t yy = 0; yy < h; yy++) {
        fb_put_pixel(x, (uint16_t)(y + yy), rgb);
        fb_put_pixel((uint16_t)(x + w - 1), (uint16_t)(y + yy), rgb);
    }
}

void gpu_draw_char(uint16_t x, uint16_t y, char ch, uint32_t fg, uint32_t bg) {
    if (!g_fb_enabled) return;
    fb_draw_char_px(x, y, ch, fg, bg);
}

void gpu_draw_text(uint16_t x, uint16_t y, const char *s, uint32_t fg, uint32_t bg) {
    if (!g_fb_enabled) return;
    for (uint16_t i = 0; s[i]; i++)
        fb_draw_char_px((uint16_t)(x + i * FONT_W), y, s[i], fg, bg);
}

static void fb_blit_row(volatile uint8_t *dst, const volatile uint8_t *src, size_t bytes) {
    size_t words = bytes / sizeof(uint32_t);
    volatile uint32_t *dw = (volatile uint32_t *)dst;
    const volatile uint32_t *sw = (const volatile uint32_t *)src;
    for (size_t i = 0; i < words; i++)
        dw[i] = sw[i];
    for (size_t i = words * sizeof(uint32_t); i < bytes; i++)
        dst[i] = src[i];
}

void gpu_flip(void) {
    if (!g_fb_enabled || !g_fb_back) return;
    size_t fb_size = (size_t)g_fb_pitch * g_fb_height;
    fb_blit_row(g_fb, g_fb_back, fb_size);
}

void gpu_flip_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_fb_enabled || !g_fb_back) return;
    if (x >= g_fb_width || y >= g_fb_height) return;
    if ((uint32_t)x + w > g_fb_width) w = (uint16_t)(g_fb_width - x);
    if ((uint32_t)y + h > g_fb_height) h = (uint16_t)(g_fb_height - y);

    size_t bpp_bytes = g_fb_bpp / 8;
    size_t row_bytes = (size_t)w * bpp_bytes;
    for (uint16_t row = 0; row < h; row++) {
        size_t off = (size_t)(y + row) * g_fb_pitch + (size_t)x * bpp_bytes;
        fb_blit_row(g_fb + off, g_fb_back + off, row_bytes);
    }
}

void gpu_set_target_back(int enable) {
    g_target_back = enable;
}

void gpu_save_region(uint32_t *dst, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_fb_enabled) return;
    for (uint16_t yy = 0; yy < h; yy++) {
        uint16_t py = (uint16_t)(y + yy);
        if (py >= g_fb_height) break;
        for (uint16_t xx = 0; xx < w; xx++) {
            uint16_t px = (uint16_t)(x + xx);
            dst[yy * w + xx] = (px < g_fb_width) ? fb_get_pixel(px, py) : 0;
        }
    }
}

void gpu_restore_region(const uint32_t *src, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_fb_enabled) return;
    for (uint16_t yy = 0; yy < h; yy++) {
        uint16_t py = (uint16_t)(y + yy);
        if (py >= g_fb_height) break;
        for (uint16_t xx = 0; xx < w; xx++) {
            uint16_t px = (uint16_t)(x + xx);
            if (px < g_fb_width)
                fb_put_pixel(px, py, src[yy * w + xx]);
        }
    }
}

/* Redireciona todo desenho subsequente (pixels, texto, console) para um
 * buffer proprio de w*h uint32_t (formato 0xRRGGBB), em vez da tela real.
 * Usado por uma janela para renderizar seu conteudo isoladamente; o
 * compositor depois usa gpu_blit_buffer para copiar isso pra tela. */
void gpu_begin_target(uint32_t *buf, uint16_t w, uint16_t h) {
    g_render_target = (volatile uint8_t *)buf;
    g_render_w = w;
    g_render_h = h;
    g_render_bpp = 32;
    g_render_pitch = (uint16_t)(w * 4);
}

void gpu_end_target(void) {
    g_render_target = NULL;
}

/* copia um buffer 0xRRGGBB (ex: o conteudo de uma janela) para a posicao
 * (dst_x, dst_y) na tela real (respeitando g_target_back). Deve ser chamado
 * com nenhum render target customizado ativo (gpu_end_target ja chamado). */
void gpu_blit_buffer(const uint32_t *buf, uint16_t bw, uint16_t bh, uint16_t dst_x, uint16_t dst_y) {
    if (!g_fb_enabled) return;
    for (uint16_t yy = 0; yy < bh; yy++) {
        uint16_t py = (uint16_t)(dst_y + yy);
        if (py >= g_fb_height) break;
        for (uint16_t xx = 0; xx < bw; xx++) {
            uint16_t px = (uint16_t)(dst_x + xx);
            if (px < g_fb_width)
                fb_put_pixel(px, py, buf[yy * bw + xx]);
        }
    }
}

/* viewport do console (em celulas) dentro do buffer/tela ativo no momento.
 * NAO mexe no cursor (g_row/g_col) - quem chama decide se quer resetar
 * (gpu_set_cursor(0,0)) ou restaurar uma posicao salva (uma janela, por
 * exemplo, precisa lembrar seu proprio cursor entre uma tecla e outra). */
void gpu_set_console_viewport(uint16_t x, uint16_t y, uint16_t cols, uint16_t rows) {
    g_vp_x = x;
    g_vp_y = y;
    g_vp_cols = cols;
    g_vp_rows = rows;
}

void gpu_reset_console_viewport(void) {
    g_vp_x = 0;
    g_vp_y = 0;
    g_vp_cols = g_fb_enabled ? (uint16_t)g_fb_cols : VGA_COLS;
    g_vp_rows = g_fb_enabled ? (uint16_t)g_fb_rows : VGA_ROWS;
}

uint16_t gpu_console_cols(void) {
    return g_fb_enabled ? g_vp_cols : VGA_COLS;
}

void gpu_set_cursor(uint16_t row, uint16_t col) {
    size_t rows = g_fb_enabled ? g_vp_rows : VGA_ROWS;
    size_t cols = g_fb_enabled ? g_vp_cols : VGA_COLS;
    if (row >= rows) row = (uint16_t)(rows - 1);
    if (col >= cols) col = (uint16_t)(cols - 1);
    g_row = row;
    g_col = col;
    fb_update_cursor();
}

void gpu_get_cursor(uint16_t *row, uint16_t *col) {
    if (row) *row = (uint16_t)g_row;
    if (col) *col = (uint16_t)g_col;
}

void gpu_erase_display(int mode) {
    size_t rows = g_fb_enabled ? g_vp_rows : VGA_ROWS;
    size_t cols = g_fb_enabled ? g_vp_cols : VGA_COLS;
    uint8_t fg = (uint8_t)(g_color & 0x0F);
    uint8_t bg = (uint8_t)(g_color >> 4);
    size_t r0 = 0, r1 = rows;
    if (mode == 0) r0 = g_row;
    else if (mode == 1) r1 = g_row + 1;
    for (size_t r = r0; r < r1; r++) {
        size_t c0 = (mode == 0 && r == g_row) ? g_col : 0;
        size_t c1 = (mode == 1 && r == g_row) ? g_col + 1 : cols;
        for (size_t c = c0; c < c1; c++) {
            if (g_fb_enabled) fb_draw_char(r, c, ' ', fg, bg);
            else VGA_MEM[r * VGA_COLS + c] = fb_cell(' ', g_color);
        }
    }
}

void gpu_erase_line(int mode) {
    size_t cols = g_fb_enabled ? g_vp_cols : VGA_COLS;
    uint8_t fg = (uint8_t)(g_color & 0x0F);
    uint8_t bg = (uint8_t)(g_color >> 4);
    size_t c0 = 0, c1 = cols;
    if (mode == 0) c0 = g_col;
    else if (mode == 1) c1 = g_col + 1;
    for (size_t c = c0; c < c1; c++) {
        if (g_fb_enabled) fb_draw_char(g_row, c, ' ', fg, bg);
        else VGA_MEM[g_row * VGA_COLS + c] = fb_cell(' ', g_color);
    }
}
