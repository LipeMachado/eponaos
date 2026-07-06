#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_READDIR 5

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static inline void sys_exit(int code) {
    register uint64_t rax __asm__("rax") = SYS_EXIT;
    register uint64_t rdi __asm__("rdi") = code;
    __asm__ volatile("int $0x80" : : "r"(rax), "r"(rdi) : "memory");
    __builtin_unreachable();
}

static inline int sys_write(int fd, const void *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_WRITE;
    register uint64_t rdi __asm__("rdi") = fd;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_open(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_OPEN;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_read(int fd, void *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_READ;
    register uint64_t rdi __asm__("rdi") = fd;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_close(int fd) {
    register uint64_t rax __asm__("rax") = SYS_CLOSE;
    register uint64_t rdi __asm__("rdi") = fd;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_readdir(const char *path, char *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_READDIR;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

#endif
