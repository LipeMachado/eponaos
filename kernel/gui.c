#include "gui.h"
#include "gpu.h"
#include "term.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "vfs.h"
#include "heap.h"
#include "pit.h"
#include <stdint.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

/* PIT roda a 100 Hz (pit_init(100) em main.c) -> 1 tick = 10 ms. */
#define FRAME_TICKS   1   /* cap ~100 FPS */
#define CLOCK_TICKS   100 /* recheca o RTC 1x por segundo */

#define MAX_WINDOWS  4
#define TITLEBAR_H   20
#define CLOSE_BTN    14

/* ==================== janelas (estilo retro/Kolibri) ==================== */

typedef enum { WIN_TERMINAL, WIN_STATIC } win_kind_t;

typedef struct {
    int used;
    int x, y;                /* topo-esquerdo da janela INTEIRA (com chrome) */
    int cw, ch;               /* tamanho da area de CLIENTE (sem chrome) */
    char title[32];
    win_kind_t kind;
    uint32_t *buf;            /* cw*ch pixels 0xRRGGBB - conteudo proprio */
    term_ctx_t term;          /* parser VT100 (so usado se kind==WIN_TERMINAL) */
    uint16_t cur_row, cur_col; /* cursor do console desta janela, entre chamadas */
    const char *static_text;  /* linhas com \n, so kind==WIN_STATIC */
} window_t;

static window_t g_win[MAX_WINDOWS];
static int g_zorder[MAX_WINDOWS]; /* indices em g_win; [0]=fundo ... [count-1]=topo */
static int g_win_count;
static int g_drag_zpos = -1;
static int g_drag_dx, g_drag_dy;
static int g_focus_zpos = -1;

static int win_outer_w(const window_t *w) { return w->cw + 2; }
static int win_outer_h(const window_t *w) { return w->ch + TITLEBAR_H + 2; }
static int win_client_x(const window_t *w) { return w->x + 1; }
static int win_client_y(const window_t *w) { return w->y + TITLEBAR_H; }
static int win_close_x(const window_t *w) { return w->x + win_outer_w(w) - CLOSE_BTN - 3; }
static int win_close_y(const window_t *w) { return w->y + 3; }

static int point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int win_create(win_kind_t kind, const char *title, int x, int y, int cw, int ch,
                       const char *static_text) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_win[i].used) continue;
        window_t *w = &g_win[i];
        w->used = 1;
        w->x = x;
        w->y = y;
        w->cw = cw;
        w->ch = ch;
        int n = 0;
        while (title[n] && n < 31) { w->title[n] = title[n]; n++; }
        w->title[n] = 0;
        w->kind = kind;
        w->buf = (uint32_t *)kmalloc((uint32_t)(cw * ch * 4));
        w->static_text = static_text;
        g_zorder[g_win_count] = i;
        g_win_count++;
        return i;
    }
    return -1;
}

static void win_raise(int zpos) {
    if (zpos == g_win_count - 1) return;
    int idx = g_zorder[zpos];
    for (int i = zpos; i < g_win_count - 1; i++)
        g_zorder[i] = g_zorder[i + 1];
    g_zorder[g_win_count - 1] = idx;
}

static void win_close(int zpos) {
    int idx = g_zorder[zpos];
    if (g_win[idx].buf) kfree(g_win[idx].buf);
    g_win[idx].used = 0;
    for (int i = zpos; i < g_win_count - 1; i++)
        g_zorder[i] = g_zorder[i + 1];
    g_win_count--;
}

static int win_hit_test(int px, int py) {
    for (int z = g_win_count - 1; z >= 0; z--) {
        window_t *w = &g_win[g_zorder[z]];
        if (!w->used) continue;
        if (point_in(px, py, w->x, w->y, win_outer_w(w), win_outer_h(w)))
            return z;
    }
    return -1;
}

static void win_render_static(window_t *w) {
    uint32_t bg_rgb = 0xAAAAAA; /* paleta indice 7 - cinza claro "old school" */
    for (int i = 0; i < w->cw * w->ch; i++)
        w->buf[i] = bg_rgb;

    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(0x00, 0x07);
    gpu_set_cursor(0, 0);
    if (w->static_text)
        for (const char *p = w->static_text; *p; p++)
            gpu_putc(*p);
    gpu_end_target();
    gpu_reset_console_viewport();
}

static void win_render_terminal_init(window_t *w) {
    uint32_t bg_rgb = 0x000000;
    for (int i = 0; i < w->cw * w->ch; i++)
        w->buf[i] = bg_rgb;

    term_ctx_init(&w->term);
    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(0x0F, 0x00);
    gpu_set_cursor(0, 0);

    term_ctx_print(&w->term, "EponaOS - demo terminal VT100/ANSI\r\n");
    term_ctx_print(&w->term,
                   "\x1b[31mvermelho \x1b[32mverde \x1b[33mamarelo \x1b[34mazul "
                   "\x1b[35mmagenta \x1b[36mciano\x1b[0m\r\n");
    term_ctx_print(&w->term, "\x1b[1;37mnegrito/brilhante\x1b[0m normal\r\n\r\n");
    term_ctx_print(&w->term, "digite algo (ESC fecha a GUI):\r\n");

    gpu_get_cursor(&w->cur_row, &w->cur_col);
    gpu_end_target();
    gpu_reset_console_viewport();
}

static void win_terminal_feed(window_t *w, char c) {
    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(w->term.fg, w->term.bg);
    gpu_set_cursor(w->cur_row, w->cur_col);
    term_ctx_putc(&w->term, c);
    gpu_get_cursor(&w->cur_row, &w->cur_col);
    gpu_end_target();
    gpu_reset_console_viewport();
}

static void draw_window_chrome(const window_t *w, int focused) {
    int ow = win_outer_w(w);
    int oh = win_outer_h(w);
    uint32_t title_bg = focused ? 0x0A2A6Eu : 0x7B8CA6u;

    gpu_draw_rect((uint16_t)w->x, (uint16_t)w->y, (uint16_t)ow, (uint16_t)oh, 0x000000);
    gpu_fill_rect((uint16_t)(w->x + 1), (uint16_t)(w->y + 1), (uint16_t)(ow - 2),
                  (uint16_t)(TITLEBAR_H - 1), title_bg);
    gpu_draw_text((uint16_t)(w->x + 5), (uint16_t)(w->y + 3), w->title, 0xFFFFFF, title_bg);

    int cbx = win_close_x(w), cby = win_close_y(w);
    gpu_fill_rect((uint16_t)cbx, (uint16_t)cby, CLOSE_BTN, CLOSE_BTN, 0xC42B1C);
    gpu_draw_rect((uint16_t)cbx, (uint16_t)cby, CLOSE_BTN, CLOSE_BTN, 0x000000);
    for (int i = 2; i < CLOSE_BTN - 2; i++) {
        gpu_put_pixel((uint16_t)(cbx + i), (uint16_t)(cby + i), 0xFFFFFF);
        gpu_put_pixel((uint16_t)(cbx + i), (uint16_t)(cby + CLOSE_BTN - 1 - i), 0xFFFFFF);
    }
}

static void compose_windows(void) {
    for (int z = 0; z < g_win_count; z++) {
        window_t *w = &g_win[g_zorder[z]];
        if (!w->used) continue;
        draw_window_chrome(w, z == g_win_count - 1);
        gpu_blit_buffer(w->buf, (uint16_t)w->cw, (uint16_t)w->ch, (uint16_t)win_client_x(w),
                        (uint16_t)win_client_y(w));
    }
}

static void windows_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        g_win[i].used = 0;
    g_win_count = 0;
    g_drag_zpos = -1;

    int ti = win_create(WIN_TERMINAL, "Terminal", 40, 60, 480, 260, NULL);
    int ai = win_create(WIN_STATIC, "Sobre o EponaOS", 560, 120, 300, 160,
                         "EponaOS\n\nGerenciador de janelas\n(prototipo v1)\n\n"
                         "Arraste pela barra\nde titulo. Clique\nno X para fechar.");

    if (ti >= 0) win_render_terminal_init(&g_win[ti]);
    if (ai >= 0) win_render_static(&g_win[ai]);

    g_focus_zpos = g_win_count > 0 ? g_win_count - 1 : -1;
}

/* ==================== relogio / topbar / cursor ==================== */

static uint32_t *g_cursor_bg;
static int g_cursor_saved;
static int g_cursor_x;
static int g_cursor_y;

typedef struct {
    int x;
    int y;
    uint8_t buttons;
} gui_pointer_t;

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t) ((v & 0x0F) + ((v >> 4) * 10));
}

static void read_time(char out[6]) {
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) {
        minute = bcd_to_bin(minute);
        hour = bcd_to_bin(hour & 0x7F);
    }

    out[0] = (char) ('0' + (hour / 10) % 10);
    out[1] = (char) ('0' + hour % 10);
    out[2] = ':';
    out[3] = (char) ('0' + (minute / 10) % 10);
    out[4] = (char) ('0' + minute % 10);
    out[5] = 0;
}

static uint32_t blend(uint32_t a, uint32_t b, uint16_t t, uint16_t max) {
    uint32_t ar = (a >> 16) & 0xFF;
    uint32_t ag = (a >> 8) & 0xFF;
    uint32_t ab = a & 0xFF;
    uint32_t br = (b >> 16) & 0xFF;
    uint32_t bg = (b >> 8) & 0xFF;
    uint32_t bb = b & 0xFF;

    if (max == 0)
        max = 1;
    uint32_t r = (ar * (max - t) + br * t) / max;
    uint32_t g = (ag * (max - t) + bg * t) / max;
    uint32_t bl = (ab * (max - t) + bb * t) / max;
    return (r << 16) | (g << 8) | bl;
}

static void draw_background(uint16_t w, uint16_t h) {
    for (uint16_t y = 24; y < h; y++) {
        uint32_t c = blend(0x2B3A55, 0x11182A, (uint16_t) (y - 24), (uint16_t) (h - 24));
        gpu_fill_rect(0, y, w, 1, c);
    }
}

static void draw_wifi_icon(uint16_t x, uint16_t y, int online) {
    uint32_t c = online ? 0x00777 : 0x606060;
    uint32_t bg = 0xB0DAF0;

    gpu_fill_rect(x, y, 26, 14, bg);
    gpu_fill_rect((uint16_t) (x + 2), (uint16_t) (y + 10), 3, 3, c);
    gpu_fill_rect((uint16_t) (x + 8), (uint16_t) (y + 7), 3, 6, c);
    gpu_fill_rect((uint16_t) (x + 14), (uint16_t) (y + 4), 3, 9, online ? 0x0088A0 : c);
    gpu_fill_rect((uint16_t) (x + 20), (uint16_t) (y + 1), 3, 12, online ? 0x00AAC0 : c);
}

static void draw_topbar(uint16_t w) {
    char time[6];
    read_time(time);

    gpu_fill_rect(0, 0, w, 24, 0x7BBFE0);
    gpu_fill_rect(0, 0, w, 2, 0xFFFFFF);
    gpu_fill_rect(0, 2, w, 7, 0xB0DAF0);
    gpu_fill_rect(0, 23, w, 1, 0x1A5276);

    uint16_t time_x = (uint16_t) (w - 54);
    gpu_draw_text(8, 5, "EponaOS", 0x003355, 0x7BBFE0);
    gpu_draw_text(time_x, 5, time, 0x003355, 0x7BBFE0);
    gpu_draw_text((uint16_t) (time_x - 54), 5, net_is_configured() ? "Wi-Fi" : "NoNet", 0x003355,
                  0x7BBFE0);
    draw_wifi_icon((uint16_t) (time_x - 84), 5, net_is_configured());
}

static int cursor_w(void) { return 16; }
static int cursor_h(void) { return 24; }

static gui_pointer_t read_pointer(uint16_t w, uint16_t h) {
    gui_pointer_t p;
    int mx, my;
    uint8_t buttons;
    mouse_get_state(&mx, &my, &buttons);

    int x = (int) (w / 2) + mx;
    int y = (int) (h / 2) - my;
    int cw = cursor_w();
    int ch = cursor_h();
    if (x < 0) x = 0;
    if (y < 24) y = 24;
    if (x > (int) w - cw) x = (int) w - cw;
    if (y > (int) h - ch) y = (int) h - ch;

    p.x = x;
    p.y = y;
    p.buttons = buttons;
    return p;
}

static void cursor_init(void) {
    if (g_cursor_bg) return;
    int cw = cursor_w();
    int ch = cursor_h();
    g_cursor_bg = (uint32_t *) kmalloc((uint32_t) (cw * ch * 4));
}

static void save_cursor_bg(int x, int y) {
    int cw = cursor_w();
    int ch = cursor_h();
    gpu_save_region(g_cursor_bg, (uint16_t) x, (uint16_t) y, (uint16_t) cw, (uint16_t) ch);
    g_cursor_x = x;
    g_cursor_y = y;
    g_cursor_saved = 1;
}

static void restore_cursor_bg(void) {
    if (!g_cursor_saved) return;
    int cw = cursor_w();
    int ch = cursor_h();
    gpu_restore_region(g_cursor_bg, (uint16_t) g_cursor_x, (uint16_t) g_cursor_y, (uint16_t) cw,
                       (uint16_t) ch);
    g_cursor_saved = 0;
}

static void draw_cursor_bitmap(int bx, int by, uint32_t fill) {
    static const char *cursor[24] = {
        "X...............", "XXX.............", "XXX.............", "XOOX............",
        "XOOOX...........", "XOOOOXX.........", "XOOOOOOX........", "XOOOOOOX........",
        "XOOOOOOOX.......", "XOOOOOOOOXX.....", "XOOOOOOOOOOX....", "XOOOOOOOOOOOX...",
        "XOOOOOOOOOOOX...", "XOOOOOOOOOOOOXX.", "XOOOOOOOOOOOOOOX", "XOOOOOOOOXXXXXXX",
        "XOOOOXXOOXX.....", "XOOOOXXOOXX.....", "XOOOX..XOOOX....", "XOOX...XOOOX....",
        "XXX.....XOOOX...", "........XOOOX...", "........XOOOX...", ".........XXX....",
    };

    for (uint16_t yy = 0; yy < 24; yy++) {
        for (uint16_t xx = 0; xx < 16; xx++) {
            char c = cursor[yy][xx];
            if (c == 'X')
                gpu_put_pixel((uint16_t) (bx + xx), (uint16_t) (by + yy), 0x000000);
            else if (c == 'O')
                gpu_put_pixel((uint16_t) (bx + xx), (uint16_t) (by + yy), fill);
        }
    }
}

static void draw_cursor_at(gui_pointer_t p) {
    cursor_init();
    save_cursor_bg(p.x, p.y);
    uint32_t fill = (p.buttons & 1) ? 0xFFE680 : 0xFFFFFF;
    draw_cursor_bitmap(p.x, p.y, fill);
}

/* ==================== loop principal ==================== */

void gui_run_desktop(void) {
    if (!gpu_is_framebuffer_enabled()) {
        gpu_print("GUI requer framebuffer VBE.\n");
        return;
    }

    uint16_t w = gpu_width();
    uint16_t h = gpu_height();
    char now[6] = {0};
    char last_time[6] = {0};
    int last_buttons = 0;
    g_cursor_saved = 0;

    int cw = cursor_w();
    int ch = cursor_h();

    gpu_set_target_back(1);
    windows_init();

    draw_background(w, h);
    draw_topbar(w);
    compose_windows();
    gpu_flip();

    read_time(now);
    for (int i = 0; i < 6; i++) last_time[i] = now[i];
    uint64_t last_clock_tick = pit_ticks();
    uint64_t last_frame_tick = pit_ticks();

    while (1) {
        uint64_t tick = pit_ticks();
        if (tick - last_clock_tick >= CLOCK_TICKS) {
            read_time(now);
            last_clock_tick = tick;
        }

        int old_x = g_cursor_x, old_y = g_cursor_y;
        int had_old = g_cursor_saved;
        restore_cursor_bg();

        int full_redraw = 0;
        if (now[0] != last_time[0] || now[1] != last_time[1] || now[3] != last_time[3] ||
            now[4] != last_time[4]) {
            for (int i = 0; i < 6; i++) last_time[i] = now[i];
            full_redraw = 1;
        }

        gui_pointer_t p = read_pointer(w, h);
        int clicked = (p.buttons & 1) && !(last_buttons & 1);
        int released = !(p.buttons & 1) && (last_buttons & 1);
        last_buttons = p.buttons;

        if (clicked) {
            int zpos = win_hit_test(p.x, p.y);
            if (zpos >= 0) {
                window_t *hw = &g_win[g_zorder[zpos]];
                int cbx = win_close_x(hw), cby = win_close_y(hw);
                if (point_in(p.x, p.y, cbx, cby, CLOSE_BTN, CLOSE_BTN)) {
                    win_close(zpos);
                    g_focus_zpos = g_win_count > 0 ? g_win_count - 1 : -1;
                } else {
                    win_raise(zpos);
                    g_focus_zpos = g_win_count - 1;
                    window_t *top = &g_win[g_zorder[g_focus_zpos]];
                    if (point_in(p.x, p.y, top->x, top->y, win_outer_w(top), TITLEBAR_H)) {
                        g_drag_zpos = g_focus_zpos;
                        g_drag_dx = p.x - top->x;
                        g_drag_dy = p.y - top->y;
                    }
                }
                full_redraw = 1;
            }
        } else if (released) {
            g_drag_zpos = -1;
        } else if ((p.buttons & 1) && g_drag_zpos >= 0) {
            window_t *dw = &g_win[g_zorder[g_drag_zpos]];
            int nx = p.x - g_drag_dx;
            int ny = p.y - g_drag_dy;
            if (nx < 0) nx = 0;
            if (ny < 24) ny = 24;
            if (nx > (int)w - win_outer_w(dw)) nx = (int)w - win_outer_w(dw);
            if (ny > (int)h - win_outer_h(dw)) ny = (int)h - win_outer_h(dw);
            if (nx != dw->x || ny != dw->y) {
                dw->x = nx;
                dw->y = ny;
                full_redraw = 1;
            }
        }

        draw_cursor_at(p);

        if (full_redraw) {
            draw_background(w, h);
            draw_topbar(w);
            compose_windows();
            draw_cursor_bitmap(p.x, p.y, (p.buttons & 1) ? 0xFFE680 : 0xFFFFFF);
            gpu_flip();
        } else {
            int rx0 = had_old ? (old_x < p.x ? old_x : p.x) : p.x;
            int ry0 = had_old ? (old_y < p.y ? old_y : p.y) : p.y;
            int rx1 = had_old ? (old_x > p.x ? old_x : p.x) + cw : p.x + cw;
            int ry1 = had_old ? (old_y > p.y ? old_y : p.y) + ch : p.y + ch;
            gpu_flip_rect((uint16_t) rx0, (uint16_t) ry0, (uint16_t) (rx1 - rx0),
                          (uint16_t) (ry1 - ry0));
        }

        uint64_t target = last_frame_tick + FRAME_TICKS;
        while (pit_ticks() < target)
            __asm__ volatile("pause");
        last_frame_tick = pit_ticks();

        int c = keyboard_getc();
        if (c == 27)
            break;
        if (c && g_focus_zpos >= 0) {
            window_t *fw = &g_win[g_zorder[g_focus_zpos]];
            if (fw->kind == WIN_TERMINAL) {
                win_terminal_feed(fw, (char)c);
                gpu_blit_buffer(fw->buf, (uint16_t)fw->cw, (uint16_t)fw->ch,
                                (uint16_t)win_client_x(fw), (uint16_t)win_client_y(fw));
                gpu_flip_rect((uint16_t)win_client_x(fw), (uint16_t)win_client_y(fw),
                              (uint16_t)fw->cw, (uint16_t)fw->ch);
            }
        }
    }

    for (int z = g_win_count - 1; z >= 0; z--)
        win_close(z);

    gpu_set_target_back(0);
    gpu_clear(0x0F, 0x00);
}
