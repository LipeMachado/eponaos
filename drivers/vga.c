#include "vga.h"
#include "io.h"
#include <stddef.h>
#include <stdint.h>

#define VGA_MEM  ((volatile uint16_t *) 0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static size_t g_row;
static size_t g_col;
static uint8_t g_color = 0x0F;

static inline uint16_t vga_cell(char c, uint8_t color) {
    return (uint16_t) (uint8_t) c | ((uint16_t) color << 8);
}

/* move o cursor piscante do hardware p/ (g_row, g_col) */
static void vga_update_cursor(void) {
    uint16_t pos = (uint16_t) (g_row * VGA_COLS + g_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    g_color = (uint8_t) (fg | (bg << 4));
}

void vga_init(void) {
    g_row = 0;
    g_col = 0;
    g_color = 0x0F;
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = vga_cell(' ', g_color);
    vga_update_cursor();
}

void vga_clear(uint8_t fg, uint8_t bg) {
    g_row = 0;
    g_col = 0;
    g_color = (uint8_t) (fg | (bg << 4));
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = vga_cell(' ', g_color);
    vga_update_cursor();
}

void vga_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= VGA_ROWS || col >= VGA_COLS)
        return;
    VGA_MEM[row * VGA_COLS + col] = vga_cell(c, (uint8_t) (fg | (bg << 4)));
}

void vga_write_at(uint8_t row, uint8_t col, const char *s, uint8_t fg, uint8_t bg) {
    for (size_t i = 0; s[i] && col + i < VGA_COLS; i++)
        vga_put_at(row, (uint8_t) (col + i), s[i], fg, bg);
}

/* sobe todas as linhas 1 posicao e limpa a ultima */
static void vga_scroll(void) {
    for (size_t i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
    for (size_t i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEM[i] = vga_cell(' ', g_color);
    g_row = VGA_ROWS - 1;
}

void vga_putc(char c) {
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\b') {
        if (g_col > 0) g_col--;
    } else if (c == '\f') {
        vga_clear(0x0F, 0x00);
        return;
    } else {
        VGA_MEM[g_row * VGA_COLS + g_col] = vga_cell(c, g_color);
        if (++g_col >= VGA_COLS) {
            g_col = 0;
            g_row++;
        }
    }
    if (g_row >= VGA_ROWS)
        vga_scroll();
    vga_update_cursor();
}

void vga_print(const char *s) {
    for (size_t i = 0; s[i]; i++)
        vga_putc(s[i]);
}
