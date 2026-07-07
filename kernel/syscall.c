#include "syscall.h"
#include "elf.h"
#include "gpu.h"
#include "term.h"
#include "serial.h"
#include "vfs.h"
#include "heap.h"
#include "keyboard.h"
#include "scheduler.h"
#include <stdint.h>
#include <stddef.h>

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

uint64_t g_elf_ret_rip = 0;
uint64_t g_elf_ret_rsp = 0;
uint64_t g_kernel_cr3 = 0;

/* callee-saved (System V ABI) do lado kernel, salvos antes de entrar em
 * ring3 e restaurados ao voltar — o programa ring3 e livre para usar
 * rbx/rbp/r12-r15 como quiser, entao nao podemos confiar que eles ainda
 * valem o que cmd_run() esperava ao "retornar" de enter_usermode_save_ret. */
uint64_t g_elf_saved_rbx = 0;
uint64_t g_elf_saved_rbp = 0;
uint64_t g_elf_saved_r12 = 0;
uint64_t g_elf_saved_r13 = 0;
uint64_t g_elf_saved_r14 = 0;
uint64_t g_elf_saved_r15 = 0;

static fd_entry_t g_fds[FD_MAX];
static int g_fd_init_done = 0;

#define MAX_PIPES 8
#define PIPE_BUF_SIZE 4096

typedef struct {
    uint8_t buf[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t count;
    int readers;
    int writers;
    int used;
} pipe_t;

static pipe_t g_pipes[MAX_PIPES];

static pipe_t *pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!g_pipes[i].used) {
            g_pipes[i].used = 1;
            g_pipes[i].head = 0;
            g_pipes[i].count = 0;
            g_pipes[i].readers = 1;
            g_pipes[i].writers = 1;
            return &g_pipes[i];
        }
    }
    return NULL;
}

static int pipe_read(pipe_t *p, uint64_t count, void *buf) {
    uint8_t *dst = (uint8_t *)buf;
    uint64_t n = 0;
    while (n < count && p->count > 0) {
        uint32_t tail = (p->head + PIPE_BUF_SIZE - p->count) % PIPE_BUF_SIZE;
        dst[n++] = p->buf[tail];
        p->count--;
    }
    return (int)n;
}

static int pipe_write(pipe_t *p, uint64_t count, const void *buf) {
    const uint8_t *src = (const uint8_t *)buf;
    uint64_t n = 0;
    while (n < count && p->count < PIPE_BUF_SIZE) {
        p->buf[p->head] = src[n++];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count++;
    }
    return (int)n;
}

void sys_fd_init(void) {
    if (g_fd_init_done) return;
    for (int i = 0; i < FD_MAX; i++)
        g_fds[i].used = 0;
    g_fd_init_done = 1;
}

int sys_fd_alloc(void *ptr, int type) {
    for (int i = 3; i < FD_MAX; i++) {
        if (!g_fds[i].used) {
            g_fds[i].used = 1;
            g_fds[i].ptr = ptr;
            g_fds[i].type = type;
            return i;
        }
    }
    return -1;
}

void sys_fd_free(int fd) {
    if (fd >= 0 && fd < FD_MAX)
        g_fds[fd].used = 0;
}

void *sys_fd_get(int fd) {
    if (fd >= 0 && fd < FD_MAX && g_fds[fd].used)
        return g_fds[fd].ptr;
    return NULL;
}

static void fd_close_all(void) {
    for (int i = 3; i < FD_MAX; i++) {
        if (!g_fds[i].used) continue;
        if (g_fds[i].type == FD_TYPE_FILE) {
            vfs_close(g_fds[i].ptr);
        } else if (g_fds[i].type == FD_TYPE_PIPE_R) {
            pipe_t *p = (pipe_t *)g_fds[i].ptr;
            p->readers--;
            if (p->readers == 0 && p->writers == 0) p->used = 0;
        } else if (g_fds[i].type == FD_TYPE_PIPE_W) {
            pipe_t *p = (pipe_t *)g_fds[i].ptr;
            p->writers--;
            if (p->readers == 0 && p->writers == 0) p->used = 0;
        }
        g_fds[i].used = 0;
    }
}

struct readdir_ctx {
    char *buf;
    uint64_t cap;
    uint64_t used;
};

static void readdir_append(struct readdir_ctx *ctx, const char *s) {
    while (*s && ctx->used + 1 < ctx->cap)
        ctx->buf[ctx->used++] = *s++;
}

static int readdir_cb(const char *name, uint32_t size, uint8_t flags, void *arg) {
    struct readdir_ctx *ctx = (struct readdir_ctx *)arg;
    char num[11];
    int i = 10;

    readdir_append(ctx, name);
    if (flags & VFS_DIR) {
        readdir_append(ctx, "/");
    } else {
        num[i] = 0;
        if (size == 0) num[--i] = '0';
        while (size && i > 0) {
            num[--i] = (char)('0' + size % 10);
            size /= 10;
        }
        readdir_append(ctx, "  ");
        readdir_append(ctx, &num[i]);
        readdir_append(ctx, " bytes");
    }
    readdir_append(ctx, "\n");
    return 0;
}

void syscall_handler_c(void *vregs) {
    struct regs *r = (struct regs *) vregs;
    switch (r->rax) {
    case SYS_EXIT:
        gpu_print("[syscall] exit\n");
        serial_print("[syscall] exit\n");
        fd_close_all();
        if (g_elf_ret_rip) {
            r->cs = 0x08;
        } else {
            __asm__ volatile("cli; hlt");
        }
        break;

    case SYS_WRITE:
        if (r->rdi == FD_STDOUT || r->rdi == FD_STDERR) {
            for (uint64_t i = 0; i < r->rdx; i++) {
                char c = ((char *)r->rsi)[i];
                term_putc(c);
                serial_putc(c);
            }
            r->rax = r->rdx;
        } else {
            int fd = (int)r->rdi;
            if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) {
                r->rax = -1;
                break;
            }
            if (g_fds[fd].type == FD_TYPE_PIPE_W) {
                pipe_t *p = (pipe_t *)g_fds[fd].ptr;
                while (p->count == PIPE_BUF_SIZE && p->readers > 0)
                    task_yield();
                if (p->readers == 0) {
                    r->rax = -1;
                    break;
                }
                r->rax = (uint64_t)pipe_write(p, r->rdx, (void*)r->rsi);
            } else if (g_fds[fd].type == FD_TYPE_FILE) {
                file_t *f = (file_t *)g_fds[fd].ptr;
                int n = vfs_write(f, r->rdx, (void*)r->rsi);
                r->rax = (uint64_t)(n < 0 ? -1 : n);
            } else {
                r->rax = -1;
            }
        }
        break;

    case SYS_OPEN: {
        char path[256];
        char *user_path = (char*)r->rdi;
        if (user_path[0] == '/') {
            int i;
            for (i = 0; i < 255 && user_path[i]; i++)
                path[i] = user_path[i];
            path[i] = 0;
        } else {
            path[0] = '/';
            int i;
            for (i = 0; i < 254 && user_path[i]; i++)
                path[1 + i] = user_path[i];
            path[1 + i] = 0;
        }

        file_t *f = vfs_open(path);
        if (f) {
            r->rax = (uint64_t)sys_fd_alloc(f, FD_TYPE_FILE);
        } else {
            r->rax = -1;
        }
        break;
    }

    case SYS_CREATE: {
        char path[256];
        char *user_path = (char*)r->rdi;
        if (user_path[0] == '/') {
            int i;
            for (i = 0; i < 255 && user_path[i]; i++)
                path[i] = user_path[i];
            path[i] = 0;
        } else {
            path[0] = '/';
            int i;
            for (i = 0; i < 254 && user_path[i]; i++)
                path[1 + i] = user_path[i];
            path[1 + i] = 0;
        }

        file_t *f = vfs_create(path);
        if (f) {
            r->rax = (uint64_t)sys_fd_alloc(f, FD_TYPE_FILE);
        } else {
            r->rax = -1;
        }
        break;
    }

    case SYS_READ: {
        if (r->rdi == FD_STDIN) {
            char *buf = (char*)r->rsi;
            uint64_t read = 0;

            while (read < r->rdx) {
                int c = keyboard_getc();
                if (!c) continue;
                if (c > 255) continue;

                buf[read++] = (char)c;
                if (c == '\n') break;
            }

            r->rax = read;
        } else {
            int fd = (int)r->rdi;
            if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) {
                r->rax = -1;
                break;
            }
            if (g_fds[fd].type == FD_TYPE_PIPE_R) {
                pipe_t *p = (pipe_t *)g_fds[fd].ptr;
                while (p->count == 0 && p->writers > 0)
                    task_yield();
                if (p->count == 0) {
                    r->rax = 0;
                    break;
                }
                r->rax = (uint64_t)pipe_read(p, r->rdx, (void*)r->rsi);
            } else if (g_fds[fd].type == FD_TYPE_FILE) {
                file_t *f = (file_t *)g_fds[fd].ptr;
                int n = vfs_read(f, r->rdx, (void*)r->rsi);
                r->rax = (uint64_t)(n < 0 ? -1 : n);
            } else {
                r->rax = -1;
            }
        }
        break;
    }

    case SYS_READDIR: {
        char path[256];
        char *user_path = (char*)r->rdi;
        char *out = (char*)r->rsi;
        uint64_t cap = r->rdx;

        if (!out || cap == 0) {
            r->rax = -1;
            break;
        }

        if (!user_path || user_path[0] == 0 || (user_path[0] == '/' && user_path[1] == 0)) {
            path[0] = '/';
            path[1] = 0;
        } else if (user_path[0] == '/') {
            int i;
            for (i = 0; i < 255 && user_path[i]; i++)
                path[i] = user_path[i];
            path[i] = 0;
        } else {
            path[0] = '/';
            int i;
            for (i = 0; i < 254 && user_path[i]; i++)
                path[1 + i] = user_path[i];
            path[1 + i] = 0;
        }

        struct readdir_ctx ctx = {.buf = out, .cap = cap, .used = 0};
        if (vfs_readdir(path, readdir_cb, &ctx) < 0) {
            out[0] = 0;
            r->rax = -1;
            break;
        }
        out[ctx.used < cap ? ctx.used : cap - 1] = 0;
        r->rax = ctx.used;
        break;
    }

    case SYS_CLOSE: {
        int fd = (int)r->rdi;
        if (fd < 0 || fd >= FD_MAX || !g_fds[fd].used) {
            r->rax = -1;
            break;
        }
        if (g_fds[fd].type == FD_TYPE_PIPE_R) {
            pipe_t *p = (pipe_t *)g_fds[fd].ptr;
            p->readers--;
            if (p->readers == 0 && p->writers == 0) p->used = 0;
            sys_fd_free(fd);
            r->rax = 0;
        } else if (g_fds[fd].type == FD_TYPE_PIPE_W) {
            pipe_t *p = (pipe_t *)g_fds[fd].ptr;
            p->writers--;
            if (p->readers == 0 && p->writers == 0) p->used = 0;
            sys_fd_free(fd);
            r->rax = 0;
        } else if (g_fds[fd].type == FD_TYPE_FILE) {
            file_t *f = (file_t *)g_fds[fd].ptr;
            vfs_close(f);
            sys_fd_free(fd);
            r->rax = 0;
        } else {
            r->rax = -1;
        }
        break;
    }

    case SYS_EXEC: {
        uint64_t entry, stack_top, pml4;
        if (elf_load((const char*)r->rdi, &entry, &stack_top, &pml4) < 0) {
            r->rax = -1;
            break;
        }

        fd_close_all();
        r->rip = entry;
        r->rsp = stack_top;
        r->rax = 0;
        __asm__ volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
        break;
    }

    case SYS_UNLINK: {
        char path[256];
        char *user_path = (char*)r->rdi;
        if (user_path[0] == '/') {
            int i;
            for (i = 0; i < 255 && user_path[i]; i++)
                path[i] = user_path[i];
            path[i] = 0;
        } else {
            path[0] = '/';
            int i;
            for (i = 0; i < 254 && user_path[i]; i++)
                path[1 + i] = user_path[i];
            path[1 + i] = 0;
        }
        r->rax = (uint64_t)(vfs_unlink(path) < 0 ? -1 : 0);
        break;
    }

    case SYS_STAT: {
        char path[256];
        char *user_path = (char*)r->rdi;
        if (user_path[0] == '/') {
            int i;
            for (i = 0; i < 255 && user_path[i]; i++)
                path[i] = user_path[i];
            path[i] = 0;
        } else {
            path[0] = '/';
            int i;
            for (i = 0; i < 254 && user_path[i]; i++)
                path[1 + i] = user_path[i];
            path[1 + i] = 0;
        }

        uint32_t *size_out = (uint32_t*)r->rsi;
        uint8_t *flags_out = (uint8_t*)r->rdx;
        uint32_t size;
        uint8_t flags;
        if (vfs_stat(path, &size, &flags) < 0) {
            r->rax = -1;
        } else {
            if (size_out) *size_out = size;
            if (flags_out) *flags_out = flags;
            r->rax = size;
        }
        break;
    }

    case SYS_PIPE: {
        int *fds = (int*)r->rdi;
        pipe_t *p = pipe_alloc();
        if (!p) { r->rax = -1; break; }
        int rfd = sys_fd_alloc(p, FD_TYPE_PIPE_R);
        int wfd = sys_fd_alloc(p, FD_TYPE_PIPE_W);
        if (rfd < 0 || wfd < 0) {
            if (rfd >= 0) sys_fd_free(rfd);
            if (wfd >= 0) sys_fd_free(wfd);
            p->used = 0;
            r->rax = -1;
            break;
        }
        fds[0] = rfd;
        fds[1] = wfd;
        r->rax = 0;
        break;
    }

    default:
        serial_print("[syscall] unknown: ");
        serial_print_hex(r->rax);
        serial_print("\n");
        break;
    }
}
