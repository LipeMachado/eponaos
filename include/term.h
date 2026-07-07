#ifndef EPONA_TERM_H
#define EPONA_TERM_H

/* Interpretador de sequencias de escape VT100/ANSI (subconjunto pratico:
 * cursor, erase, cores SGR) sobre o console de gpu.c/gpu.h. Cada "consumidor"
 * (o shell do kernel, uma janela do GUI, um processo ring-3 via SYS_WRITE)
 * mantem seu proprio estado de parser chamando term_ctx_* explicitamente,
 * ou usa as funcoes globais term_putc/term_print para o console padrao. */

typedef struct {
    int state;
    int params[16];
    int nparams;
    int param_active;
    unsigned char fg;
    unsigned char bg;
    unsigned short saved_row;
    unsigned short saved_col;
} term_ctx_t;

void term_ctx_init(term_ctx_t *ctx);
void term_ctx_putc(term_ctx_t *ctx, char c);
void term_ctx_print(term_ctx_t *ctx, const char *s);

/* console padrao (tela cheia) - usa um term_ctx_t global interno */
void term_putc(char c);
void term_print(const char *s);
void term_reset(void);

#endif
