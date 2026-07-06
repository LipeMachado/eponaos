#include "scheduler.h"
#include "pmm.h"
#include "vga.h"
#include "serial.h"
#include <stddef.h>

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

    context_switch(&prev->rsp, next->rsp);
}

void task_yield(void) {
    schedule();
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

void scheduler_init(void) {
    serial_print("[scheduler] init\n");
    task_create(idle_task);
    g_current->state = TASK_RUNNING;
    serial_print("[scheduler] pronto\n");
}
