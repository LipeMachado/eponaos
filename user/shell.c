#include "libc.h"

static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int ends_with(const char *s, const char *suffix) {
    int sl = strlen(s);
    int tl = strlen(suffix);
    if (tl > sl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static void strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) {}
}

static void strcat(char *dst, const char *src) {
    while (*dst) dst++;
    strcpy(dst, src);
}

static void puts(const char *s) {
    sys_write(STDOUT_FILENO, s, (uint64_t)strlen(s));
}

static void putc(char c) {
    sys_write(STDOUT_FILENO, &c, 1);
}

static void clear(void) {
    putc('\f');
}

#define LINE_MAX 256
#define TOKEN_MAX 16
#define EPP_MAX_INSTALLED 16

struct installed_pkg {
    char name[32];
    char version[16];
    char file[64];
    int used;
};

static struct installed_pkg g_installed[EPP_MAX_INSTALLED];

static void readline(char *buf, int max) {
    int pos = 0;
    buf[0] = 0;
    while (1) {
        char c;
        if (sys_read(STDIN_FILENO, &c, 1) != 1)
            continue;
        if (c == '\n') {
            putc('\n');
            buf[pos] = 0;
            return;
        }
        if (c == '\b') {
            if (pos > 0) { pos--; putc('\b'); }
            continue;
        }
        if (pos < max - 1) {
            buf[pos++] = c;
            putc(c);
        }
    }
}

static int tokenize(char *line, char **tokens, int max) {
    int count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (count >= max - 1) break;
        tokens[count++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    tokens[count] = 0;
    return count;
}

static void cmd_help(void) {
    puts("Commands:\n");
    puts("  help              show this help\n");
    puts("  clear             clear the screen\n");
    puts("  pwd               print working directory\n");
    puts("  ls [path]         list directory\n");
    puts("  cat <file>        print file\n");
    puts("  write <file> <t>  create/overwrite file\n");
    puts("  rm <file>         delete file\n");
    puts("  stat <file>       show file info\n");
    puts("  run <elf>         execute ELF\n");
    puts("  epp <cmd> [pkg]   Epona Package manager\n");
    puts("  <file>.epk        execute package manifest\n");
    puts("  echo <text>       print text\n");
    puts("  edit              text editor (prompts for file)\n");
    puts("  exit              return to kernel shell\n");
}

static int read_file(const char *path, char *buf, int cap) {
    int fd = sys_open(path);
    if (fd < 0) return -1;

    int total = 0;
    while (total < cap - 1) {
        int n = sys_read(fd, buf + total, (uint64_t)(cap - 1 - total));
        if (n < 0) {
            sys_close(fd);
            return -1;
        }
        if (n == 0) break;
        total += n;
    }
    buf[total] = 0;
    sys_close(fd);
    return total;
}

static void print_value(const char *manifest, const char *key) {
    int key_len = strlen(key);
    const char *p = manifest;
    while (*p) {
        if (starts_with(p, key) && p[key_len] == '=') {
            p += key_len + 1;
            while (*p && *p != '\n') putc(*p++);
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    puts("unknown");
}

static int get_value(const char *manifest, const char *key, char *out, int cap) {
    int key_len = strlen(key);
    const char *p = manifest;
    while (*p) {
        if (starts_with(p, key) && p[key_len] == '=') {
            int i = 0;
            p += key_len + 1;
            while (*p && *p != '\n' && i < cap - 1)
                out[i++] = *p++;
            out[i] = 0;
            return 0;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    out[0] = 0;
    return -1;
}

static void package_path(const char *name, char *out) {
    strcpy(out, name);
    if (!ends_with(out, ".epk"))
        strcat(out, ".epk");
}

static int installed_find(const char *name) {
    for (int i = 0; i < EPP_MAX_INSTALLED; i++) {
        if (g_installed[i].used && strcmp(g_installed[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int installed_add(const char *name, const char *version, const char *file) {
    int idx = installed_find(name);
    if (idx < 0) {
        for (int i = 0; i < EPP_MAX_INSTALLED; i++) {
            if (!g_installed[i].used) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) return -1;

    g_installed[idx].used = 1;
    strcpy(g_installed[idx].name, name);
    strcpy(g_installed[idx].version, version);
    strcpy(g_installed[idx].file, file);
    return 0;
}

static int installed_remove(const char *name) {
    int idx = installed_find(name);
    if (idx < 0) return -1;
    g_installed[idx].used = 0;
    return 0;
}

static void installed_list(void) {
    int count = 0;
    for (int i = 0; i < EPP_MAX_INSTALLED; i++) {
        if (!g_installed[i].used) continue;
        puts("  ");
        puts(g_installed[i].name);
        puts(" ");
        puts(g_installed[i].version);
        puts(" -> ");
        puts(g_installed[i].file);
        putc('\n');
        count++;
    }
    if (count == 0)
        puts("  (none)\n");
}

static void epk_execute(const char *path) {
    char manifest[512];
    char file[64];

    if (read_file(path, manifest, sizeof(manifest)) < 0) {
        puts("epp: cannot open package: ");
        puts(path);
        putc('\n');
        return;
    }

    puts("Package: "); print_value(manifest, "name"); putc('\n');
    puts("Version: "); print_value(manifest, "version"); putc('\n');
    puts("About:   "); print_value(manifest, "description"); putc('\n');

    if (get_value(manifest, "file", file, sizeof(file)) < 0) {
        puts("epp: package has no file entry\n");
        return;
    }

    int fd = sys_open(file);
    if (fd < 0) {
        puts("epp: payload missing: ");
        puts(file);
        putc('\n');
        return;
    }
    sys_close(fd);

    puts("Status:  installed/ready (payload ");
    puts(file);
    puts(")\n");
}

static int epk_install(const char *path) {
    char manifest[512];
    char name[32];
    char version[16];
    char file[64];

    if (read_file(path, manifest, sizeof(manifest)) < 0) {
        puts("epp: cannot open package: ");
        puts(path);
        putc('\n');
        return -1;
    }

    if (get_value(manifest, "name", name, sizeof(name)) < 0 ||
        get_value(manifest, "version", version, sizeof(version)) < 0 ||
        get_value(manifest, "file", file, sizeof(file)) < 0) {
        puts("epp: invalid package manifest\n");
        return -1;
    }

    int fd = sys_open(file);
    if (fd < 0) {
        puts("epp: payload missing: ");
        puts(file);
        putc('\n');
        return -1;
    }
    sys_close(fd);

    if (installed_add(name, version, file) < 0) {
        puts("epp: installed database is full\n");
        return -1;
    }

    puts("Installed ");
    puts(name);
    puts(" ");
    puts(version);
    putc('\n');
    return 0;
}

static void cmd_epp(char **tokens, int argc) {
    char path[64];
    char repo[512];

    if (argc < 2 || strcmp(tokens[1], "help") == 0) {
        puts("Epona Package (epp)\n");
        puts("  epp update          refresh local repo index\n");
        puts("  epp list            list packages\n");
        puts("  epp installed       list installed packages\n");
        puts("  epp info <pkg>      show package metadata\n");
        puts("  epp install <pkg>   verify/install local package\n");
        puts("  epp remove <pkg>    remove package from db\n");
        puts("  epp upgrade         check all repo packages\n");
        return;
    }

    if (strcmp(tokens[1], "update") == 0) {
        int n = read_file("repo.epk", repo, sizeof(repo));
        if (n < 0) {
            puts("epp: repo.epk not found\n");
            return;
        }
        puts("Repository updated from repo.epk\n");
        return;
    }

    if (strcmp(tokens[1], "list") == 0) {
        if (read_file("repo.epk", repo, sizeof(repo)) < 0) {
            puts("epp: run epp update first\n");
            return;
        }
        const char *p = repo;
        while (*p) {
            if (starts_with(p, "package=")) {
                p += 8;
                puts("  ");
                while (*p && *p != '\n') putc(*p++);
                putc('\n');
            }
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
        return;
    }

    if (strcmp(tokens[1], "installed") == 0) {
        installed_list();
        return;
    }

    if (strcmp(tokens[1], "upgrade") == 0) {
        if (read_file("repo.epk", repo, sizeof(repo)) < 0) {
            puts("epp: repo.epk not found\n");
            return;
        }

        const char *p = repo;
        while (*p) {
            if (starts_with(p, "package=")) {
                int i = 0;
                char pkg[32];
                p += 8;
                while (*p && *p != '\n' && i < (int)sizeof(pkg) - 1)
                    pkg[i++] = *p++;
                pkg[i] = 0;
                package_path(pkg, path);
                epk_install(path);
            }
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
        puts("epp: upgrade complete (local repository)\n");
        return;
    }

    if (strcmp(tokens[1], "remove") == 0) {
        if (argc < 3) {
            puts("usage: epp remove <pkg>\n");
            return;
        }
        if (installed_remove(tokens[2]) < 0) {
            puts("epp: package is not installed: ");
            puts(tokens[2]);
            putc('\n');
            return;
        }
        puts("Removed ");
        puts(tokens[2]);
        putc('\n');
        return;
    }

    if (strcmp(tokens[1], "info") == 0 || strcmp(tokens[1], "install") == 0) {
        if (argc < 3) {
            puts("usage: epp ");
            puts(tokens[1]);
            puts(" <pkg>\n");
            return;
        }
        package_path(tokens[2], path);
        if (strcmp(tokens[1], "info") == 0) {
            epk_execute(path);
        } else if (epk_install(path) == 0) {
            puts("epp: install complete\n");
        }
        return;
    }

    puts("epp: unknown command: ");
    puts(tokens[1]);
    putc('\n');
}

static void cmd_ls(char **tokens, int argc) {
    char buf[1024];
    const char *path = argc > 1 ? tokens[1] : "/";
    int n = sys_readdir(path, buf, sizeof(buf));
    if (n < 0) {
        puts("ls: cannot read directory\n");
        return;
    }
    if (n == 0) {
        puts("(empty)\n");
        return;
    }
    puts(buf);
}

static void cmd_cat(char **tokens, int argc) {
    if (argc < 2) {
        puts("usage: cat <file>\n");
        return;
    }

    int fd = sys_open(tokens[1]);
    if (fd < 0) {
        puts("cat: cannot open ");
        puts(tokens[1]);
        putc('\n');
        return;
    }

    char buf[128];
    while (1) {
        int n = sys_read(fd, buf, sizeof(buf));
        if (n < 0) {
            puts("cat: read error\n");
            break;
        }
        if (n == 0) break;
        sys_write(STDOUT_FILENO, buf, (uint64_t)n);
    }
    sys_close(fd);
    putc('\n');
}

static void cmd_write(char **tokens, int argc) {
    if (argc < 3) {
        puts("usage: write <file> <text>\n");
        return;
    }

    int fd = sys_create(tokens[1]);
    if (fd < 0) {
        puts("write: cannot create ");
        puts(tokens[1]);
        putc('\n');
        return;
    }

    for (int i = 2; i < argc; i++) {
        if (i > 2)
            sys_write(fd, " ", 1);
        sys_write(fd, tokens[i], (uint64_t)strlen(tokens[i]));
    }
    sys_write(fd, "\n", 1);
    sys_close(fd);
}

static void cmd_run(char **tokens, int argc) {
    if (argc < 2) {
        puts("usage: run <elf>\n");
        return;
    }

    if (ends_with(tokens[1], ".epk")) {
        puts("run: .epk files are package manifests, not ELF binaries\n");
        return;
    }

    puts("Executing ELF...\n");
    if (sys_exec(tokens[1]) < 0) {
        puts("run: cannot execute ");
        puts(tokens[1]);
        putc('\n');
    }
}

static void cmd_rm(char **tokens, int argc) {
    if (argc < 2) {
        puts("usage: rm <file>\n");
        return;
    }

    if (sys_unlink(tokens[1]) < 0) {
        puts("rm: cannot remove ");
        puts(tokens[1]);
        putc('\n');
    }
}

static void cmd_stat(char **tokens, int argc) {
    if (argc < 2) {
        puts("usage: stat <file>\n");
        return;
    }

    uint32_t size;
    uint8_t flags;
    int res = sys_stat(tokens[1], &size, &flags);
    if (res < 0) {
        puts("stat: cannot stat ");
        puts(tokens[1]);
        putc('\n');
        return;
    }

    puts(tokens[1]);
    if (flags & 2) puts(" (dir)");
    else puts(" (file)");
    putc('\n');
    puts("  size: ");

    char num[12];
    int ni = 11;
    num[ni] = 0;
    uint32_t n = size;
    if (n == 0) num[--ni] = '0';
    while (n) {
        num[--ni] = (char)('0' + n % 10);
        n /= 10;
    }
    puts(&num[ni]);
    puts(" bytes\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char line[LINE_MAX];
    char *tokens[TOKEN_MAX];

    while (1) {
        puts("user@epona:~$ ");
        readline(line, LINE_MAX);
        if (line[0] == 0) continue;

        int argc2 = tokenize(line, tokens, TOKEN_MAX);
        if (argc2 == 0) continue;

        if (strcmp(tokens[0], "exit") == 0) {
            puts("Goodbye!\n");
            return 0;
        }
        else if (strcmp(tokens[0], "help") == 0) {
            cmd_help();
        }
        else if (strcmp(tokens[0], "clear") == 0) {
            clear();
        }
        else if (strcmp(tokens[0], "pwd") == 0) {
            puts("/\n");
        }
        else if (strcmp(tokens[0], "ls") == 0) {
            cmd_ls(tokens, argc2);
        }
        else if (strcmp(tokens[0], "cat") == 0) {
            cmd_cat(tokens, argc2);
        }
        else if (strcmp(tokens[0], "write") == 0) {
            cmd_write(tokens, argc2);
        }
        else if (strcmp(tokens[0], "run") == 0) {
            cmd_run(tokens, argc2);
        }
        else if (strcmp(tokens[0], "rm") == 0) {
            cmd_rm(tokens, argc2);
        }
        else if (strcmp(tokens[0], "stat") == 0) {
            cmd_stat(tokens, argc2);
        }
        else if (strcmp(tokens[0], "epp") == 0) {
            cmd_epp(tokens, argc2);
        }
        else if (strcmp(tokens[0], "edit") == 0) {
            sys_exec("EDIT.ELF");
        }
        else if (strcmp(tokens[0], "echo") == 0) {
            for (int i = 1; i < argc2; i++) {
                if (i > 1) putc(' ');
                puts(tokens[i]);
            }
            putc('\n');
        }
        else if (ends_with(tokens[0], ".epk")) {
            epk_execute(tokens[0]);
        }
        else {
            puts("Unknown: ");
            puts(tokens[0]);
            putc('\n');
        }
    }
}
