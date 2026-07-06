#include "syscall.h"
#include "elf.h"
#include "vga.h"
#include "serial.h"
#include "vfs.h"
#include "heap.h"
#include "keyboard.h"
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

static fd_entry_t g_fds[FD_MAX];
static int g_fd_init_done = 0;

void sys_fd_init(void) {
    if (g_fd_init_done) return;
    for (int i = 0; i < FD_MAX; i++)
        g_fds[i].used = 0;
    g_fd_init_done = 1;
}

int sys_fd_alloc(void *file_ptr) {
    for (int i = 3; i < FD_MAX; i++) {
        if (!g_fds[i].used) {
            g_fds[i].used = 1;
            g_fds[i].ptr = file_ptr;
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
        if (g_fds[i].used) {
            vfs_close(g_fds[i].ptr);
            g_fds[i].used = 0;
        }
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
        vga_print("[syscall] exit\n");
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
                vga_putc(c);
                serial_putc(c);
            }
            r->rax = r->rdx;
        } else {
            file_t *f = sys_fd_get((int)r->rdi);
            if (f) {
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
            r->rax = (uint64_t)sys_fd_alloc(f);
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
            file_t *f = sys_fd_get((int)r->rdi);
            if (!f) {
                r->rax = -1;
                break;
            }

            int n = vfs_read(f, r->rdx, (void*)r->rsi);
            r->rax = (uint64_t)(n < 0 ? -1 : n);
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
        file_t *f = sys_fd_get((int)r->rdi);
        if (f) {
            vfs_close(f);
            sys_fd_free((int)r->rdi);
            r->rax = 0;
        } else {
            r->rax = -1;
        }
        break;
    }

    default:
        serial_print("[syscall] unknown: ");
        serial_print_hex(r->rax);
        serial_print("\n");
        break;
    }
}
