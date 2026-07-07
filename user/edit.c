#include "libc.h"

#define MAX_LINES 512
#define LINE_MAX 256
#define BUF_SIZE 65536

static char g_lines[MAX_LINES][LINE_MAX];
static int g_num;
static int g_dirty;

static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void strcpy(char *d, const char *s) {
    while ((*d++ = *s++)) {}
}

static void puts(const char *s) {
    sys_write(STDOUT_FILENO, s, (uint64_t)strlen(s));
}

static void putc(char c) {
    sys_write(STDOUT_FILENO, &c, 1);
}

static void readline(char *buf, int max) {
    int pos = 0;
    while (1) {
        char c;
        if (sys_read(STDIN_FILENO, &c, 1) != 1) continue;
        if (c == '\n') { buf[pos] = 0; putc('\n'); return; }
        if (c == '\b') { if (pos > 0) { pos--; putc('\b'); } continue; }
        if (pos < max - 1) { buf[pos++] = c; putc(c); }
    }
}

static int read_file(const char *path) {
    int fd = sys_open(path);
    if (fd < 0) return -1;

    char *buf = (char *)0x60000000;
    int total = 0;
    while (total < BUF_SIZE - 1) {
        int n = sys_read(fd, buf + total, (uint64_t)(BUF_SIZE - 1 - total));
        if (n <= 0) break;
        total += n;
    }
    buf[total] = 0;
    sys_close(fd);

    g_num = 0;
    int pos = 0;
    for (int i = 0; buf[i] && g_num < MAX_LINES; i++) {
        if (buf[i] == '\n') {
            g_lines[g_num][pos] = 0;
            g_num++;
            pos = 0;
        } else if (pos < LINE_MAX - 1) {
            g_lines[g_num][pos++] = buf[i];
        }
    }
    if (pos > 0 || (total > 0 && buf[total-1] == '\n')) {
        g_lines[g_num][pos] = 0;
        if (pos > 0 || g_num == 0) g_num++;
    }
    return 0;
}

static int save_file(const char *path) {
    int fd = sys_create(path);
    if (fd < 0) return -1;
    for (int i = 0; i < g_num; i++) {
        sys_write(fd, g_lines[i], (uint64_t)strlen(g_lines[i]));
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
    g_dirty = 0;
    return 0;
}

static void print_lines(void) {
    for (int i = 0; i < g_num; i++) {
        char num[8];
        int ni = 6;
        num[ni] = 0;
        int n = i + 1;
        while (ni > 0 && n > 0) { num[--ni] = (char)('0' + n % 10); n /= 10; }
        while (ni > 0) num[--ni] = ' ';
        puts(num);
        puts("  ");
        puts(g_lines[i]);
        putc('\n');
    }
}

static void list_range(int start, int end) {
    if (start < 0) start = 0;
    if (end >= g_num) end = g_num - 1;
    for (int i = start; i <= end; i++) {
        char num[8];
        int ni = 6;
        num[ni] = 0;
        int n = i + 1;
        while (ni > 0 && n > 0) { num[--ni] = (char)('0' + n % 10); n /= 10; }
        while (ni > 0) num[--ni] = ' ';
        puts(num);
        puts("  ");
        puts(g_lines[i]);
        putc('\n');
    }
}

static void insert_lines(int after) {
    puts("(enter lines, '.' alone to end)\n");
    while (1) {
        char line[LINE_MAX];
        puts("> ");
        readline(line, LINE_MAX);
        if (strcmp(line, ".") == 0) break;
        if (g_num >= MAX_LINES) { puts("Buffer full\n"); break; }
        for (int i = g_num; i > after; i--)
            strcpy(g_lines[i], g_lines[i-1]);
        strcpy(g_lines[after], line);
        g_num++;
        after++;
    }
    g_dirty = 1;
}

static void delete_line(int n) {
    if (n < 0 || n >= g_num) { puts("Invalid line\n"); return; }
    for (int i = n; i < g_num - 1; i++)
        strcpy(g_lines[i], g_lines[i+1]);
    g_num--;
    g_dirty = 1;
}

static void help(void) {
    puts("Editor commands:\n");
    puts("  p        print all lines\n");
    puts("  n        print line n\n");
    puts("  i [n]    insert after line n (default: 0)\n");
    puts("  a [n]    append after line n (default: end)\n");
    puts("  d n      delete line n\n");
    puts("  w        save (write) file\n");
    puts("  q        quit (discards if unsaved)\n");
    puts("  Q        force quit without saving\n");
    puts("  h        this help\n");
    puts("  n,m p    print range n through m\n");
}

int main(int argc, char **argv) {
    char pathbuf[64];
    const char *path;
    (void)argc;
    path = (argc >= 2) ? argv[1] : "";
    if (path[0] == 0) {
        puts("File: ");
        readline(pathbuf, 64);
        if (pathbuf[0] == 0) return 0;
        path = pathbuf;
    }
    if (read_file(path) < 0) {
        puts("(new file)\n");
        g_num = 0;
    } else {
        puts("--- ");
        puts(path);
        puts(" ---\n");
    }
    g_dirty = 0;
    while (1) {
        char line[LINE_MAX];
        puts("edit> ");
        readline(line, LINE_MAX);
        if (line[0] == 0) { print_lines(); continue; }
        if (strcmp(line, "q") == 0) {
            if (g_dirty) { puts("Unsaved changes. Use Q to force quit.\n"); continue; }
            break;
        }
        if (strcmp(line, "Q") == 0) break;
        if (strcmp(line, "w") == 0) {
            if (save_file(path) < 0) puts("Save failed\n");
            else puts("Saved.\n");
            continue;
        }
        if (strcmp(line, "p") == 0) { print_lines(); continue; }
        if (strcmp(line, "h") == 0) { help(); continue; }
        if (line[0] == 'i') {
            int n = 0;
            if (line[1] == ' ') {
                int v = 0;
                for (int j = 2; line[j] >= '0' && line[j] <= '9'; j++)
                    v = v * 10 + (line[j] - '0');
                n = v - 1;
            }
            if (n < 0) n = 0;
            if (n > g_num) n = g_num;
            insert_lines(n);
            continue;
        }
        if (line[0] == 'a') {
            int n = g_num - 1;
            if (line[1] == ' ') {
                int v = 0;
                for (int j = 2; line[j] >= '0' && line[j] <= '9'; j++)
                    v = v * 10 + (line[j] - '0');
                n = v - 1;
            }
            if (n < 0) n = 0;
            if (n >= g_num) n = g_num - 1;
            insert_lines(n + 1);
            continue;
        }
        if (line[0] == 'd') {
            int v = 0;
            int j = 1;
            while (line[j] == ' ') j++;
            for (; line[j] >= '0' && line[j] <= '9'; j++)
                v = v * 10 + (line[j] - '0');
            if (v < 1 || v > g_num) { puts("Invalid line\n"); continue; }
            delete_line(v - 1);
            continue;
        }
        {
            int n = 0, j = 0, m = -1;
            while (line[j] >= '0' && line[j] <= '9') { n = n * 10 + (line[j] - '0'); j++; }
            if (line[j] == ',') {
                m = n;
                j++;
                n = 0;
                while (line[j] >= '0' && line[j] <= '9') { n = n * 10 + (line[j] - '0'); j++; }
                while (line[j] == ' ') j++;
                if (line[j] == 'p') {
                    if (m < 1 || n > g_num || m > n) { puts("Invalid range\n"); continue; }
                    list_range(m - 1, n - 1);
                    continue;
                }
            } else if (line[j] == 0 && n >= 1 && n <= g_num) {
                list_range(n - 1, n - 1);
                continue;
            }
        }
        puts("?\n");
    }
    return 0;
}
