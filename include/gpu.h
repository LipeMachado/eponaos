#ifndef EPONA_GPU_H
#define EPONA_GPU_H

#include <stdint.h>

/* GPU/framebuffer console API. Colors use the classic 4-bit VGA palette. */
void gpu_init(void);
void gpu_init_framebuffer(void);
void gpu_set_color(uint8_t fg, uint8_t bg);
void gpu_putc(char c);
void gpu_print(const char *s);
void gpu_clear(uint8_t fg, uint8_t bg);
void gpu_put_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
void gpu_write_at(uint8_t row, uint8_t col, const char *s, uint8_t fg, uint8_t bg);

int gpu_is_framebuffer_enabled(void);
uint16_t gpu_width(void);
uint16_t gpu_height(void);
uint8_t gpu_bpp(void);
void gpu_put_pixel(uint16_t x, uint16_t y, uint32_t rgb);
uint32_t gpu_get_pixel(uint16_t x, uint16_t y);
void gpu_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb);
void gpu_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb);
void gpu_draw_char(uint16_t x, uint16_t y, char ch, uint32_t fg, uint32_t bg);
void gpu_draw_text(uint16_t x, uint16_t y, const char *s, uint32_t fg, uint32_t bg);
void gpu_flip(void);
void gpu_flip_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void gpu_set_target_back(int enable);
void gpu_save_region(uint32_t *dst, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void gpu_restore_region(const uint32_t *src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* render target proprio (buffer de janela) + viewport de console */
void gpu_begin_target(uint32_t *buf, uint16_t w, uint16_t h);
void gpu_end_target(void);
void gpu_blit_buffer(const uint32_t *buf, uint16_t bw, uint16_t bh, uint16_t dst_x, uint16_t dst_y);
void gpu_set_console_viewport(uint16_t x, uint16_t y, uint16_t cols, uint16_t rows);
void gpu_reset_console_viewport(void);
void gpu_set_cursor(uint16_t row, uint16_t col);
void gpu_get_cursor(uint16_t *row, uint16_t *col);
void gpu_erase_display(int mode);
void gpu_erase_line(int mode);

#endif
