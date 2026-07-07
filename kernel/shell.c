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

#define LINE_MAX 256
#define TOKEN_MAX 16
#define HIST_MAX 8

static char g_history[HIST_MAX][LINE_MAX];
static int g_hist_count, g_hist_idx;

static int ends_with(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t sul = strlen(suffix);
    if (sul > sl) return 0;
    return strcmp(s + sl - sul, suffix) == 0;
}

static void print_prompt(void) {
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
static void redraw_line(const char *buf, int pos, int cpos) {
    (void)pos;
    int plen = (int) strlen(buf);
    int pcols = 15;
    gpu_print("\r");
    print_prompt();
    for (int i = 0; i < plen; i++)
        gpu_putc(buf[i]);
    for (int i = pcols + plen; i < 79; i++)
        gpu_putc(' ');
    gpu_print("\r");
    print_prompt();
    for (int i = 0; i < cpos; i++)
        gpu_putc(buf[i]);
}

/* insere ch em buf[pos], desloca resto para direita */
static void insert_char(char *buf, int *pos, int *cpos, int ch, int max) {
    if (*pos >= max - 1) return;
    for (int i = *pos; i > *cpos; i--)
        buf[i] = buf[i - 1];
    buf[*cpos] = (char)ch;
    (*pos)++;
    (*cpos)++;
    buf[*pos] = '\0';
}

/* remove caractere antes do cursor */
static void backspace_char(char *buf, int *pos, int *cpos) {
    if (*cpos <= 0) return;
    for (int i = *cpos - 1; i < *pos - 1; i++)
        buf[i] = buf[i + 1];
    (*pos)--;
    (*cpos)--;
    buf[*pos] = '\0';
}

/* remove caractere sob o cursor */
static void delete_char(char *buf, int *pos, int *cpos) {
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
    strncpy(g_history[idx], line, LINE_MAX - 1);
    g_history[idx][LINE_MAX - 1] = 0;
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
                backspace_char(buf, &pos, &cpos);
                redraw_line(buf, pos, cpos);
            }
            continue;
        }

        if (c == KEY_DEL) {
            delete_char(buf, &pos, &cpos);
            redraw_line(buf, pos, cpos);
            continue;
        }

        if (c == '\t') continue;

        /* setas */
        if (c == KEY_LEFT) {
            if (cpos > 0) { cpos--; redraw_line(buf, pos, cpos); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cpos < pos) { cpos++; redraw_line(buf, pos, cpos); }
            continue;
        }
        if (c == KEY_HOME) {
            cpos = 0;
            redraw_line(buf, pos, cpos);
            continue;
        }
        if (c == KEY_END) {
            cpos = pos;
            redraw_line(buf, pos, cpos);
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
            redraw_line(buf, pos, cpos);
            continue;
        }

        if (c >= 32 && c < 127 && pos < max - 1) {
            insert_char(buf, &pos, &cpos, c, max);
            redraw_line(buf, pos, cpos);
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
    gpu_print("  ls                lista arquivos\n");
    gpu_print("  cat <arquivo>     le arquivo\n");
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

static void cmd_ls(void) {
    struct ls_ctx ctx;
    ctx.count = 0;
    gpu_print("/:\n");
    vfs_readdir("/", ls_cb, &ctx);
    if (ctx.count == 0) gpu_print("  (vazio)\n");
}

static void cmd_cat(const char *path) {
    char full[64];
    full[0] = '/';
    int pl = (int) strlen(path);
    if (pl > 62) pl = 62;
    memcpy(full + 1, path, (size_t) pl);
    full[1 + pl] = 0;

    file_t *f = vfs_open(full);
    if (!f) {
        gpu_print("Arquivo nao encontrado: ");
        gpu_print(path);
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
    if (argc < 2) { gpu_print("Uso: run <elf>\n"); return; }

    if (ends_with(args[1], ".epk")) {
        gpu_print("Pacotes .epk nao sao executaveis ELF. Use: run shell.elf\n");
        return;
    }

    uint64_t entry, stack_top, pml4;
    if (elf_load(args[1], &entry, &stack_top, &pml4) < 0) {
        gpu_print("Falha ao carregar ELF: ");
        gpu_print(args[1]);
        gpu_print("\n");
        return;
    }

    gpu_print("Executando ELF...\n");
    enter_usermode_save_ret((void*)entry, (void*)stack_top, (void*)pml4);
}

void shell_run(void) {
    char line[LINE_MAX];
    char *tokens[TOKEN_MAX];

    while (1) {
        print_prompt();
        readline(line, LINE_MAX);
        if (line[0] == '\0') continue;

        history_add(line);

        int argc = tokenize(line, tokens, TOKEN_MAX);
        if (argc == 0) continue;

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
            cmd_ls();
        } else if (strcmp(cmd, "cat") == 0) {
            cmd_cat(argc > 1 ? tokens[1] : "");
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
}
