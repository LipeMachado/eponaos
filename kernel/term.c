#include "term.h"
#include "gpu.h"
#include <stddef.h>

/* Maquina de estados de sequencias de escape VT100/ANSI, no espirito do
 * parser de Paul Williams (vt100.net/emu/dec_ansi_parser) que praticamente
 * todo terminal serio (xterm, VTE, Alacritty/kitty via a crate `vte`)
 * implementa. Aqui so o subconjunto realmente usado por um shell:
 * cursor (CSI A/B/C/D/H/f/s/u), erase (CSI J/K) e cores (CSI m). Sequencias
 * DCS/Sixel/etc nao sao suportadas - caem no estado OSC/ignoradas. */

enum {
    TS_GROUND = 0,
    TS_ESCAPE,
    TS_CSI,
    TS_OSC,
};

static term_ctx_t g_default_ctx;
static int g_default_init;

static void params_reset(term_ctx_t *ctx) {
    for (int i = 0; i < 16; i++)
        ctx->params[i] = 0;
    ctx->nparams = 0;
    ctx->param_active = 0;
}

static void param_digit(term_ctx_t *ctx, int d) {
    if (ctx->nparams == 0) {
        ctx->nparams = 1;
        ctx->param_active = 0;
    }
    if (!ctx->param_active) {
        ctx->params[ctx->nparams - 1] = 0;
        ctx->param_active = 1;
    }
    if (ctx->nparams <= 16)
        ctx->params[ctx->nparams - 1] = ctx->params[ctx->nparams - 1] * 10 + d;
}

static void param_sep(term_ctx_t *ctx) {
    if (ctx->nparams < 16) {
        ctx->nparams++;
        ctx->param_active = 0;
        ctx->params[ctx->nparams - 1] = 0;
    }
}

static void apply_sgr(term_ctx_t *ctx) {
    if (ctx->nparams == 0) {
        ctx->fg = 0x0F;
        ctx->bg = 0x00;
    }
    for (int i = 0; i < ctx->nparams; i++) {
        int p = ctx->params[i];
        if (p == 0) { ctx->fg = 0x0F; ctx->bg = 0x00; }
        else if (p == 1) { ctx->fg = (unsigned char)(ctx->fg | 0x08); }
        else if (p >= 30 && p <= 37) { ctx->fg = (unsigned char)((ctx->fg & 0x08) | (p - 30)); }
        else if (p == 39) { ctx->fg = (unsigned char)((ctx->fg & 0x08) | 0x07); }
        else if (p >= 40 && p <= 47) { ctx->bg = (unsigned char)(p - 40); }
        else if (p == 49) { ctx->bg = 0x00; }
        else if (p >= 90 && p <= 97) { ctx->fg = (unsigned char)(8 + (p - 90)); }
        else if (p >= 100 && p <= 107) { ctx->bg = (unsigned char)(8 + (p - 100)); }
    }
    gpu_set_color(ctx->fg, ctx->bg);
}

static void csi_dispatch(term_ctx_t *ctx, unsigned char final) {
    int n0 = (ctx->nparams > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
    uint16_t row, col;
    switch (final) {
    case 'A':
        gpu_get_cursor(&row, &col);
        gpu_set_cursor((uint16_t)((row >= n0) ? row - n0 : 0), col);
        break;
    case 'B':
        gpu_get_cursor(&row, &col);
        gpu_set_cursor((uint16_t)(row + n0), col);
        break;
    case 'C':
        gpu_get_cursor(&row, &col);
        gpu_set_cursor(row, (uint16_t)(col + n0));
        break;
    case 'D':
        gpu_get_cursor(&row, &col);
        gpu_set_cursor(row, (uint16_t)((col >= n0) ? col - n0 : 0));
        break;
    case 'H':
    case 'f': {
        uint16_t r = (uint16_t)((ctx->nparams > 0 && ctx->params[0] > 0) ? ctx->params[0] - 1 : 0);
        uint16_t c = (uint16_t)((ctx->nparams > 1 && ctx->params[1] > 0) ? ctx->params[1] - 1 : 0);
        gpu_set_cursor(r, c);
        break;
    }
    case 'J':
        gpu_erase_display(ctx->nparams > 0 ? ctx->params[0] : 0);
        break;
    case 'K':
        gpu_erase_line(ctx->nparams > 0 ? ctx->params[0] : 0);
        break;
    case 'm':
        apply_sgr(ctx);
        break;
    case 's':
        gpu_get_cursor(&ctx->saved_row, &ctx->saved_col);
        break;
    case 'u':
        gpu_set_cursor(ctx->saved_row, ctx->saved_col);
        break;
    default:
        break;
    }
}

void term_ctx_init(term_ctx_t *ctx) {
    ctx->state = TS_GROUND;
    ctx->nparams = 0;
    ctx->param_active = 0;
    ctx->fg = 0x0F;
    ctx->bg = 0x00;
    ctx->saved_row = 0;
    ctx->saved_col = 0;
}

void term_ctx_putc(term_ctx_t *ctx, char c) {
    unsigned char b = (unsigned char)c;
    switch (ctx->state) {
    case TS_GROUND:
        if (b == 0x1B) { ctx->state = TS_ESCAPE; break; }
        gpu_putc(c);
        break;
    case TS_ESCAPE:
        if (b == '[') { params_reset(ctx); ctx->state = TS_CSI; }
        else if (b == ']') { ctx->state = TS_OSC; }
        else { ctx->state = TS_GROUND; }
        break;
    case TS_CSI:
        if (b >= '0' && b <= '9') { param_digit(ctx, b - '0'); }
        else if (b == ';') { param_sep(ctx); }
        else if (b >= 0x40 && b <= 0x7E) { csi_dispatch(ctx, b); ctx->state = TS_GROUND; }
        /* intermediate bytes (0x20-0x2F) ou lixo: ignora, fica em CSI */
        break;
    case TS_OSC:
        if (b == 0x07 || b == 0x1B) { ctx->state = TS_GROUND; }
        break;
    default:
        ctx->state = TS_GROUND;
        break;
    }
}

void term_ctx_print(term_ctx_t *ctx, const char *s) {
    for (size_t i = 0; s[i]; i++)
        term_ctx_putc(ctx, s[i]);
}

static term_ctx_t *default_ctx(void) {
    if (!g_default_init) {
        term_ctx_init(&g_default_ctx);
        g_default_init = 1;
    }
    return &g_default_ctx;
}

void term_putc(char c) {
    term_ctx_putc(default_ctx(), c);
}

void term_print(const char *s) {
    term_ctx_print(default_ctx(), s);
}

void term_reset(void) {
    term_ctx_init(default_ctx());
    gpu_set_color(0x0F, 0x00);
}
