#ifndef EPONA_SCHEDULER_H
#define EPONA_SCHEDULER_H

#include <stdint.h>

#define TASK_STACK_SIZE 4096  /* 4 KiB = 1 frame PMM */

typedef enum {
    TASK_READY    = 0,
    TASK_RUNNING  = 1,
    TASK_BLOCKED  = 2,
    TASK_TERMINATED = 3
} task_state_t;

typedef struct task_struct {
    uint32_t       pid;
    task_state_t   state;
    uint64_t       rsp;        /* stack pointer (RSP) quando nao executando */
    uint64_t       cr3;        /* CR3 (PML4 phys) for this task, 0 = kernel */
    void           (*entry)(void);  /* funcao de entrada */
    struct task_struct *next;  /* proximo na lista circular */
} task_struct_t;

void scheduler_init(void);
uint32_t task_create(void (*entry)(void));
void schedule(void);
void task_yield(void);

#endif
