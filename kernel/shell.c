#include "shell.h"
#include "keyboard.h"
#include "gpu.h"
#include "serial.h"
#include "pmm.h"
#include "net.h"
#include "vfs.h"
#include "string.h"
#include "elf.h"
#include "syscall.h"
#include "gui.h"
#include <stdint.h>

#define HIST_MAX 8

static char g_history[HIST_MAX][SHELL_LINE_MAX];
static int g_hist_count, g_hist_idx;
static int g_shell_context_gui;

static int ends_with(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t sul = strlen(suffix);
    if (sul > sl) return 0;
    return strcmp(s + sl - sul, suffix) == 0;
}

void shell_set_gui_context(int enabled) {
    g_shell_context_gui = enabled;
}

void shell_print_prompt(void) {
    gpu_set_color(0x0A, 0x00);
    gpu_print("epona@epona");
    gpu_set_color(0x0F, 0x00);
    gpu_print(":");
    gpu_set_color(0x0B, 0x00);
    gpu_print("~");
    gpu_set_color(0x0F, 0x00);
    gpu_print("$ ");
}

/* re-desenha a linha: \r + prompt + buf + limpa resto + \r + prompt + cursor */
void shell_redraw_line(const char *buf, int pos, int cpos) {
    (void)pos;
    int plen = (int) strlen(buf);
    int pcols = 15;
    int cols = (int) gpu_console_cols() - 1;
    gpu_print("\r");
    shell_print_prompt();
    for (int i = 0; i < plen; i++)
        gpu_putc(buf[i]);
    for (int i = pcols + plen; i < cols; i++)
        gpu_putc(' ');
    gpu_print("\r");
    shell_print_prompt();
    for (int i = 0; i < cpos; i++)
        gpu_putc(buf[i]);
}

/* insere ch em buf[pos], desloca resto para direita */
void shell_insert_char(char *buf, int *pos, int *cpos, int ch, int max) {
    if (*pos >= max - 1) return;
    for (int i = *pos; i > *cpos; i--)
        buf[i] = buf[i - 1];
    buf[*cpos] = (char)ch;
    (*pos)++;
    (*cpos)++;
    buf[*pos] = '\0';
}

/* remove caractere antes do cursor */
void shell_backspace_char(char *buf, int *pos, int *cpos) {
    if (*cpos <= 0) return;
    for (int i = *cpos - 1; i < *pos - 1; i++)
        buf[i] = buf[i + 1];
    (*pos)--;
    (*cpos)--;
    buf[*pos] = '\0';
}

/* remove caractere sob o cursor */
void shell_delete_char(char *buf, int *pos, int *cpos) {
    if (*cpos >= *pos) return;
    for (int i = *cpos; i < *pos - 1; i++)
        buf[i] = buf[i + 1];
    (*pos)--;
    buf[*pos] = '\0';
}

static void history_add(const char *line) {
    if (line[0] == '\0') return;
    if (g_hist_count > 0 && strcmp(g_history[(g_hist_count - 1) % HIST_MAX], line) == 0)
        return;
    int idx = g_hist_count % HIST_MAX;
    strncpy(g_history[idx], line, SHELL_LINE_MAX - 1);
    g_history[idx][SHELL_LINE_MAX - 1] = 0;
    g_hist_count++;
    g_hist_idx = g_hist_count;
}

static const char *history_get(int idx) {
    if (idx < 0 || idx >= g_hist_count) return "";
    return g_history[idx % HIST_MAX];
}

static void readline(char *buf, int max) {
    int pos = 0, cpos = 0;
    buf[0] = '\0';

    while (1) {
        int c = keyboard_getc();
        if (!c) continue;

        if (c == '\n') {
            gpu_putc('\n');
            buf[pos] = '\0';
            return;
        }

        if (c == '\b') {
            if (cpos > 0) {
                shell_backspace_char(buf, &pos, &cpos);
                shell_redraw_line(buf, pos, cpos);
            }
            continue;
        }

        if (c == KEY_DEL) {
            shell_delete_char(buf, &pos, &cpos);
            shell_redraw_line(buf, pos, cpos);
            continue;
        }

        if (c == '\t') continue;

        /* setas */
        if (c == KEY_LEFT) {
            if (cpos > 0) { cpos--; shell_redraw_line(buf, pos, cpos); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cpos < pos) { cpos++; shell_redraw_line(buf, pos, cpos); }
            continue;
        }
        if (c == KEY_HOME) {
            cpos = 0;
            shell_redraw_line(buf, pos, cpos);
            continue;
        }
        if (c == KEY_END) {
            cpos = pos;
            shell_redraw_line(buf, pos, cpos);
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            if (c == KEY_UP && g_hist_idx > 0) g_hist_idx--;
            else if (c == KEY_DOWN && g_hist_idx < g_hist_count) g_hist_idx++;
            else continue;
            const char *h = history_get(g_hist_idx);
            int hl = (int) strlen(h);
            if (hl >= max) hl = max - 1;
            memcpy(buf, h, (size_t)hl);
            buf[hl] = 0;
            pos = hl;
            cpos = hl;
            shell_redraw_line(buf, pos, cpos);
            continue;
        }

        if (c >= 32 && c < 127 && pos < max - 1) {
            shell_insert_char(buf, &pos, &cpos, c, max);
            shell_redraw_line(buf, pos, cpos);
        }
    }
}

static void print_ip(uint32_t ip) {
    char buf[4];
    for (int part = 0; part < 4; part++) {
        uint8_t n = (uint8_t)((ip >> (part * 8)) & 0xFF);
        int i = 0;
        if (n >= 100) buf[i++] = (char)('0' + n / 100);
        if (n >= 10)  buf[i++] = (char)('0' + (n / 10) % 10);
        buf[i++] = (char)('0' + n % 10);
        buf[i] = 0;
        gpu_print(buf);
        if (part < 3) gpu_print(".");
    }
}

static void print_u64(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v) {
        buf[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    gpu_print(&buf[i]);
}

static int parse_ip(const char *s, uint32_t *out) {
    uint32_t ip = 0;
    for (int part = 0; part < 4; part++) {
        int n = 0;
        if (!*s || *s < '0' || *s > '9') return -1;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s++ - '0');
            if (n > 255) return -1;
        }
        if (part < 3 && *s++ != '.') return -1;
        ip |= (uint32_t) n << (part * 8);
    }
    *out = ip;
    return 0;
}

static int tokenize(char *line, char **tokens, int max) {
    int count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (count >= max - 1) break;
        tokens[count++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    tokens[count] = 0;
    return count;
}

static void cmd_help(void) {
    gpu_print("Comandos:\n");
    gpu_print("  help              ajuda\n");
    gpu_print("  clear             limpa tela\n");
    gpu_print("  gpuinfo           info do framebuffer\n");
    gpu_print("  gui               preview da interface grafica\n");
    gpu_print("  neofetch          info do sistema\n");
    gpu_print("  ip                config de rede\n");
    gpu_print("  http              status HTTP\n");
    gpu_print("  ls [dir]          lista arquivos\n");
    gpu_print("  cd [dir]          muda diretorio atual\n");
    gpu_print("  cat <arquivo>     le arquivo\n");
    gpu_print("  mkdir <dir>       cria diretorio\n");
    gpu_print("  touch <arquivo>   cria arquivo vazio\n");
    gpu_print("  echo <texto>      ecoa texto\n");
    gpu_print("  ping <ip>         ping ICMP\n");
    gpu_print("  dns <host>        consulta DNS\n");
    gpu_print("  fetch <host>      HTTP GET\n");
    gpu_print("  run <elf>         executa ELF\n");
}

static void cmd_clear(void) {
    gpu_clear(0x0F, 0x00);
}

static void cmd_gpuinfo(void) {
    gpu_print("GPU: ");
    gpu_print(gpu_is_framebuffer_enabled() ? "framebuffer VBE\n" : "VGA text mode\n");
    gpu_print("Resolucao: ");
    print_u64(gpu_width());
    gpu_print("x");
    print_u64(gpu_height());
    gpu_print("x");
    print_u64(gpu_bpp());
    gpu_print("\n");
}

static void cmd_neofetch(void) {
    gpu_set_color(0x0B, 0x00);
    gpu_print("          //\n");
    gpu_print("         //\n");
    gpu_print("        //\n");
    gpu_print("       //\n");
    gpu_print("      //\n");
    gpu_print("     //\n");
    gpu_print("    //\n");
    gpu_print("   //\n");
    gpu_print("  //\n");
    gpu_print(" //\n");
    gpu_print("////////////////////////////////////////////////////\n");
    gpu_set_color(0x0F, 0x00);

    gpu_print("SO:         EponaOS 0.1 x86_64\n");
    gpu_print("Kernel:     Epona\n");
    gpu_print("Shell:      epona-sh\n");

    gpu_print("Memoria:    ");
    print_u64(pmm_total_bytes() / (1024 * 1024));
    gpu_print(" MiB (");
    print_u64(pmm_free_bytes() / (1024 * 1024));
    gpu_print(" MiB livre)\n");

    if (net_is_configured()) {
        gpu_set_color(0x0A, 0x00);
        gpu_print("Rede:       conectada (");
        print_ip(net_local_ip());
        gpu_print(")\n");
        gpu_set_color(0x0F, 0x00);
    } else {
        gpu_set_color(0x0C, 0x00);
        gpu_print("Rede:       desconectada\n");
        gpu_set_color(0x0F, 0x00);
    }

    if (net_dns_answer_ip()) {
        gpu_print("DNS:        example.com = ");
        print_ip(net_dns_answer_ip());
        gpu_print("\n");
    }
    gpu_print("Terminal:   VGA text-mode 80x25\n");
}

static void cmd_ip(void) {
    if (!net_is_configured()) {
        gpu_print("Rede nao configurada\n");
        return;
    }
    gpu_print("IP:         ");
    print_ip(net_local_ip());
    gpu_print("\n");
    if (net_dns_answer_ip()) {
        gpu_print("DNS:        example.com = ");
        print_ip(net_dns_answer_ip());
        gpu_print("\n");
    }
}

static void cmd_http(void) {
    gpu_print("HTTP: ");
    if (net_http_ok()) {
        gpu_set_color(0x0A, 0x00);
        gpu_print("OK");
    } else {
        gpu_set_color(0x0C, 0x00);
        gpu_print("sem resposta");
    }
    gpu_set_color(0x0F, 0x00);
    gpu_print("\n");

    uint32_t len = net_http_body_len();
    if (len > 0) {
        gpu_print("Bytes:      ");
        print_u64(len);
        gpu_print("\nPreview:    ");
        gpu_print(net_http_body_preview());
        gpu_print("\n");
    }
}

#define CWD_MAX 256
static char g_cwd[CWD_MAX] = "/";

/* colapsa "." / ".." / "//" de um path absoluto ja combinado em out;
 * nunca sobe acima da raiz. Retorna 0 ou -1 (estouro / muitos componentes). */
static int normalize_path(const char *combined, char *out, int max) {
    const char *segs[32];
    int seg_len[32];
    int nseg = 0;
    const char *p = combined;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);
        if (len == 1 && start[0] == '.') {
            /* ignora */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (nseg > 0) nseg--;
        } else {
            if (nseg >= 32) return -1;
            segs[nseg] = start;
            seg_len[nseg] = len;
            nseg++;
        }
    }
    int pos = 0;
    if (pos >= max) return -1;
    out[pos++] = '/';
    for (int i = 0; i < nseg; i++) {
        if (i > 0) {
            if (pos >= max - 1) return -1;
            out[pos++] = '/';
        }
        if (pos + seg_len[i] >= max) return -1;
        memcpy(out + pos, segs[i], (size_t)seg_len[i]);
        pos += seg_len[i];
    }
    out[pos] = 0;
    return 0;
}

/* resolve `input` (absoluto ou relativo a g_cwd) em `out`, absoluto e
 * normalizado. input NULL/vazio => resolve para o proprio g_cwd. */
static int resolve_path(const char *input, char *out, int max) {
    char combined[CWD_MAX + SHELL_LINE_MAX];
    if (!input || input[0] == 0) {
        strncpy(combined, g_cwd, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = 0;
    } else if (input[0] == '/') {
        strncpy(combined, input, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = 0;
    } else {
        size_t gl = strlen(g_cwd), il = strlen(input);
        if (gl + 1 + il >= sizeof(combined)) return -1;
        memcpy(combined, g_cwd, gl);
        combined[gl] = '/';
        memcpy(combined + gl + 1, input, il + 1);
    }
    return normalize_path(combined, out, max);
}

struct ls_ctx { int count; };

static int ls_cb(const char *name, uint32_t size, uint8_t flags, void *arg) {
    struct ls_ctx *ctx = (struct ls_ctx *) arg;
    gpu_print("  ");
    if (flags & VFS_DIR)
        gpu_set_color(0x0B, 0x00);
    gpu_print(name);
    gpu_set_color(0x0F, 0x00);
    if (!(flags & VFS_DIR)) {
        gpu_print(" (");
        print_u64(size);
        gpu_print(" bytes)");
    }
    gpu_print("\n");
    ctx->count++;
    return 0;
}

static void cmd_ls(const char *arg) {
    char resolved[CWD_MAX];
    if (resolve_path(arg, resolved, CWD_MAX) < 0) { gpu_print("ls: caminho invalido\n"); return; }
    struct ls_ctx ctx;
    ctx.count = 0;
    gpu_print(resolved);
    gpu_print(":\n");
    vfs_readdir(resolved, ls_cb, &ctx);
    if (ctx.count == 0) gpu_print("  (vazio)\n");
}

static void cmd_cd(char **args, int argc) {
    char resolved[CWD_MAX];
    const char *target = argc > 1 ? args[1] : "/";
    if (resolve_path(target, resolved, CWD_MAX) < 0) { gpu_print("cd: caminho invalido\n"); return; }
    uint32_t size; uint8_t flags;
    if (vfs_stat(resolved, &size, &flags) < 0) {
        gpu_print("cd: nao encontrado: "); gpu_print(resolved); gpu_print("\n");
        return;
    }
    if (!(flags & VFS_DIR)) {
        gpu_print("cd: nao e um diretorio: "); gpu_print(resolved); gpu_print("\n");
        return;
    }
    strncpy(g_cwd, resolved, CWD_MAX - 1);
    g_cwd[CWD_MAX - 1] = 0;
}

static void cmd_mkdir(char **args, int argc) {
    if (argc < 2) { gpu_print("Uso: mkdir <dir>\n"); return; }
    char resolved[CWD_MAX];
    if (resolve_path(args[1], resolved, CWD_MAX) < 0) { gpu_print("mkdir: caminho invalido\n"); return; }
    if (vfs_mkdir(resolved) < 0) {
        gpu_print("mkdir: falha ao criar: "); gpu_print(resolved); gpu_print("\n");
    }
}

static void cmd_touch(char **args, int argc) {
    if (argc < 2) { gpu_print("Uso: touch <arquivo>\n"); return; }
    char resolved[CWD_MAX];
    if (resolve_path(args[1], resolved, CWD_MAX) < 0) { gpu_print("touch: caminho invalido\n"); return; }
    file_t *f = vfs_create(resolved);
    if (!f) {
        gpu_print("touch: falha ao criar: "); gpu_print(resolved); gpu_print("\n");
        return;
    }
    vfs_close(f);
}

static void cmd_cat(const char *path) {
    char resolved[CWD_MAX];
    if (resolve_path(path, resolved, CWD_MAX) < 0) { gpu_print("cat: caminho invalido\n"); return; }

    file_t *f = vfs_open(resolved);
    if (!f) {
        gpu_print("Arquivo nao encontrado: ");
        gpu_print(resolved);
        gpu_print("\n");
        return;
    }
    char buf[256];
    int n = vfs_read(f, 255, buf);
    if (n > 0) {
        buf[n] = 0;
        gpu_print(buf);
        if (buf[n - 1] != '\n') gpu_print("\n");
    }
    vfs_close(f);
}

static void cmd_echo(char **args, int argc) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) gpu_print(" ");
        gpu_print(args[i]);
    }
    gpu_print("\n");
}

static void cmd_ping(char **args, int argc) {
    if (argc < 2) { gpu_print("Uso: ping <ip>\n"); return; }
    if (!net_is_configured()) { gpu_print("Rede nao configurada\n"); return; }
    uint32_t ip;
    if (parse_ip(args[1], &ip) < 0) { gpu_print("IP invalido\n"); return; }
    gpu_print("Pinging "); print_ip(ip); gpu_print("...\n");

    net_ping_send(ip);
    for (int i = 0; i < 2000000; i++) {
        net_poll();
        if (net_ping_replied()) {
            gpu_set_color(0x0A, 0x00);
            gpu_print("Resposta recebida!\n");
            gpu_set_color(0x0F, 0x00);
            return;
        }
    }
    gpu_set_color(0x0C, 0x00);
    gpu_print("Sem resposta (timeout)\n");
    gpu_set_color(0x0F, 0x00);
}

static void cmd_dns(char **args, int argc) {
    if (argc < 2) { gpu_print("Uso: dns <host>\n"); return; }
    if (!net_is_configured()) { gpu_print("Rede nao configurada\n"); return; }
    gpu_print("Resolvendo: "); gpu_print(args[1]); gpu_print("\n");

    dns_query(args[1]);
    for (int i = 0; i < 3000000; i++) {
        net_poll();
        uint32_t ip = net_dns_answer_ip();
        if (ip) { gpu_print("Resposta: "); print_ip(ip); gpu_print("\n"); return; }
    }
    gpu_set_color(0x0C, 0x00);
    gpu_print("Timeout\n");
    gpu_set_color(0x0F, 0x00);
}

static void cmd_fetch(char **args, int argc) {
    if (argc < 2) { gpu_print("Uso: fetch <host>\n"); return; }
    if (!net_is_configured()) { gpu_print("Rede nao configurada\n"); return; }
    gpu_print("Buscando: "); gpu_print(args[1]); gpu_print("\n");

    if (net_http_fetch(args[1]) < 0) { gpu_print("Erro\n"); return; }
    for (int i = 0; i < 5000000; i++) {
        net_poll();
        if (net_tcp_busy()) continue;
        if (net_http_ok()) {
            gpu_set_color(0x0A, 0x00);
            gpu_print("HTTP 200 OK\n");
            gpu_set_color(0x0F, 0x00);
            gpu_print("Bytes: "); print_u64(net_http_body_len()); gpu_print("\n");
            gpu_print("Preview: "); gpu_print(net_http_body_preview()); gpu_print("\n");
            return;
        }
        if (!net_tcp_busy() && i > 100000) {
            gpu_set_color(0x0C, 0x00);
            gpu_print("Falha\n");
            gpu_set_color(0x0F, 0x00);
            return;
        }
    }
    gpu_set_color(0x0C, 0x00);
    gpu_print("Timeout\n");
    gpu_set_color(0x0F, 0x00);
}

static void cmd_run(char **args, int argc) {
    if (g_shell_context_gui) {
        gpu_print("run: desabilitado no Terminal da GUI (travaria a interface); use o console de texto.\n");
        return;
    }
    if (argc < 2) { gpu_print("Uso: run <elf>\n"); return; }

    if (ends_with(args[1], ".epk")) {
        gpu_print("Pacotes .epk nao sao executaveis ELF. Use: run shell.elf\n");
        return;
    }

    char resolved[CWD_MAX];
    if (resolve_path(args[1], resolved, CWD_MAX) < 0) { gpu_print("run: caminho invalido\n"); return; }

    uint64_t entry, stack_top, pml4;
    if (elf_load(resolved, &entry, &stack_top, &pml4) < 0) {
        gpu_print("Falha ao carregar ELF: ");
        gpu_print(resolved);
        gpu_print("\n");
        return;
    }

    gpu_print("Executando ELF...\n");
    enter_usermode_save_ret((void*)entry, (void*)stack_top, (void*)pml4);
}

void shell_dispatch_line(char *line) {
    char *tokens[SHELL_TOKEN_MAX];
    history_add(line);

    int argc = tokenize(line, tokens, SHELL_TOKEN_MAX);
    if (argc == 0) return;

    const char *cmd = tokens[0];
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "gpuinfo") == 0) {
        cmd_gpuinfo();
    } else if (strcmp(cmd, "gui") == 0) {
        gui_run_desktop();
    } else if (strcmp(cmd, "neofetch") == 0) {
        cmd_neofetch();
    } else if (strcmp(cmd, "ip") == 0) {
        cmd_ip();
    } else if (strcmp(cmd, "http") == 0) {
        cmd_http();
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls(argc > 1 ? tokens[1] : "");
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd(tokens, argc);
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(argc > 1 ? tokens[1] : "");
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(tokens, argc);
    } else if (strcmp(cmd, "touch") == 0) {
        cmd_touch(tokens, argc);
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo(tokens, argc);
    } else if (strcmp(cmd, "ping") == 0) {
        cmd_ping(tokens, argc);
    } else if (strcmp(cmd, "dns") == 0) {
        cmd_dns(tokens, argc);
    } else if (strcmp(cmd, "fetch") == 0) {
        cmd_fetch(tokens, argc);
    } else if (strcmp(cmd, "run") == 0) {
        cmd_run(tokens, argc);
    } else {
        gpu_set_color(0x0C, 0x00);
        gpu_print("Comando nao encontrado: ");
        gpu_set_color(0x0F, 0x00);
        gpu_print(cmd);
        gpu_print("\n");
    }
}

void shell_run(void) {
    char line[SHELL_LINE_MAX];

    while (1) {
        shell_print_prompt();
        readline(line, SHELL_LINE_MAX);
        if (line[0] == '\0') continue;
        shell_dispatch_line(line);
    }
}
