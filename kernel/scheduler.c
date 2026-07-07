#include "scheduler.h"
#include "pmm.h"
#include "gpu.h"
#include "serial.h"
#include "pit.h"
#include <stddef.h>

/* CR3 (PML4 fisico) do proprio kernel, capturado uma vez no boot antes de
 * qualquer processo ring-3 existir. Usado como "endereco padrao" para
 * qualquer task com cr3 == 0 (thread kernel pura). Definido em syscall.c. */
extern uint64_t g_kernel_cr3;

static task_struct_t *g_current = NULL;
static uint32_t g_next_pid = 1;

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

uint32_t task_create(void (*entry)(void)) {
    task_struct_t *task = (task_struct_t *) pmm_alloc();
    uint64_t *stack = (uint64_t *) pmm_alloc();

    if (!task || !stack)
        return 0;

    uint64_t stack_top = (uint64_t) stack + TASK_STACK_SIZE;

    stack_top -= 8;
    *(uint64_t *) stack_top = (uint64_t) entry;

    for (int i = 0; i < 6; i++) {
        stack_top -= 8;
        *(uint64_t *) stack_top = 0;
    }

    task->pid   = g_next_pid++;
    task->state = TASK_READY;
    task->rsp   = stack_top;
    task->cr3   = 0; /* 0 = roda no espaco de enderecos do kernel */
    task->entry = entry;
    task->next  = NULL;

    if (g_current == NULL) {
        task->next = task;
        g_current = task;
    } else {
        task->next = g_current->next;
        g_current->next = task;
    }

    return task->pid;
}

void schedule(void) {
    if (g_current == NULL)
        return;

    task_struct_t *start = g_current;
    task_struct_t *next  = g_current->next;

    while (next != start && next->state != TASK_READY) {
        next = next->next;
    }

    if (next == start) {
        return;
    }

    task_struct_t *prev = g_current;

    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    g_current   = next;

    /* Cada task pode estar rodando sob um CR3 diferente (ex: um processo
     * ring-3 interrompido por IRQ no meio da execucao). Salva o CR3 ao vivo
     * (o que estava ativo no exato momento desta troca) na task que esta
     * saindo, e restaura o CR3 da task que esta entrando antes do switch
     * de pilha propriamente dito. cr3 == 0 significa "kernel padrao". */
    uint64_t next_rsp = next->rsp;
    uint64_t next_cr3 = next->cr3 ? next->cr3 : g_kernel_cr3;
    uint64_t cur_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cur_cr3));
    prev->cr3 = cur_cr3;
    if (next_cr3 != cur_cr3)
        __asm__ volatile("mov %0, %%cr3" :: "r"(next_cr3) : "memory");

    context_switch(&prev->rsp, next_rsp);
}

void task_yield(void) {
    schedule();
}

void task_sleep(uint64_t ms) {
    if (!g_current) return;
    uint64_t ticks_to_sleep = ms / 10;
    if (ticks_to_sleep == 0) ticks_to_sleep = 1;
    g_current->wake_tick = pit_ticks() + ticks_to_sleep;
    g_current->state = TASK_SLEEPING;
    task_yield();
}

void scheduler_tick(void) {
    uint64_t now = pit_ticks();
    if (!g_current) return;
    task_struct_t *t = g_current;
    do {
        if (t->state == TASK_SLEEPING && now >= t->wake_tick)
            t->state = TASK_READY;
        t = t->next;
    } while (t != g_current);
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

void scheduler_init(void) {
    serial_print("[scheduler] init\n");
    /* captura o CR3 do kernel antes de qualquer processo ring-3 existir */
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_kernel_cr3));
    task_create(idle_task);
    g_current->state = TASK_RUNNING;
    serial_print("[scheduler] pronto\n");
}
