#ifndef EPONA_VGA_H
#define EPONA_VGA_H
#include <stdint.h>

/* cores 4-bit: 0=preto 1=azul 2=verde 3=ciano 4=verm 7=cinza 15=branco */
void vga_init(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_print(const char *s);
void vga_clear(uint8_t fg, uint8_t bg);
void vga_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
void vga_write_at(uint8_t row, uint8_t col, const char *s, uint8_t fg, uint8_t bg);
#endif
