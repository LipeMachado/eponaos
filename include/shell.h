#ifndef EPONA_SHELL_H
#define EPONA_SHELL_H

#define SHELL_LINE_MAX  256
#define SHELL_TOKEN_MAX 16

void shell_run(void);

/* despacha uma linha ja completa (history_add + tokenize + dispatch de
 * comando); usada pelo loop de texto (shell_run) E pela janela Terminal
 * da GUI (gui.c), para nao duplicar a logica dos cmd_*. */
void shell_dispatch_line(char *line);

/* helpers de edicao de linha, reusados por qualquer "console" de texto
 * (a tela cheia ou uma janela) desde que o viewport/console ja esteja
 * setado pelo chamador antes de invoca-los. */
void shell_print_prompt(void);
void shell_redraw_line(const char *buf, int pos, int cpos);
void shell_insert_char(char *buf, int *pos, int *cpos, int ch, int max);
void shell_backspace_char(char *buf, int *pos, int *cpos);
void shell_delete_char(char *buf, int *pos, int *cpos);

/* liga/desliga o "modo GUI" do dispatcher: cmd_run() recusa executar ELFs
 * quando isto esta ativo, pois travaria o loop de eventos da GUI (ver
 * nota em kernel/gui.c). */
void shell_set_gui_context(int enabled);

#endif
