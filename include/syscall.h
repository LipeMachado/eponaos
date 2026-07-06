#ifndef EPONA_SYSCALL_H
#define EPONA_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_READDIR 5

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

#define FD_MAX 16

typedef struct {
    void *ptr;
    int   used;
} fd_entry_t;

void enter_usermode(void *func, void *stack);
void enter_usermode_save_ret(void *func, void *stack, void *pml4);
void syscall_handler_c(void *regs);

extern uint64_t g_elf_ret_rip;
extern uint64_t g_elf_ret_rsp;
extern uint64_t g_kernel_cr3;

int  sys_fd_alloc(void *file_ptr);
void sys_fd_free(int fd);
void *sys_fd_get(int fd);
void sys_fd_init(void);

#endif
