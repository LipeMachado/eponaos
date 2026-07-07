#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>

#define HEAP_START   0xFFFF880000000000ULL
#define HEAP_INITIAL (1ULL << 20)      /* 1 MiB inicial */
#define HEAP_ALIGN   16
#define HEADER_SIZE  sizeof(heap_header_t)
#define MIN_BLOCK    64

typedef struct heap_header {
    size_t size;
    uint8_t free;
    struct heap_header *next;
    struct heap_header *prev;
} __attribute__((packed)) heap_header_t;

static heap_header_t *g_heap_start = NULL;
static heap_header_t *g_heap_end = NULL;
static uint64_t g_heap_virt = HEAP_START;
static uint64_t g_heap_virt_top = HEAP_START;

static heap_header_t *heap_create_block(uint64_t virt, size_t size) {
    heap_header_t *h = (heap_header_t *) virt;
    h->size = size;
    h->free = 1;
    h->next = NULL;
    h->prev = NULL;
    return h;
}

static void heap_coalesce(heap_header_t *h) {
    if (h->next && h->next->free) {
        h->size += h->next->size;
        h->next = h->next->next;
        if (h->next)
            h->next->prev = h;
    }
}

void heap_init(void) {
    serial_print("[heap] init\n");
    paging_map_range(g_heap_virt, (uint64_t) pmm_alloc_contiguous(256), 256, PAGE_RW);
    g_heap_virt_top = g_heap_virt + 256 * PAGE_SIZE;
    g_heap_start = heap_create_block(g_heap_virt, 256 * PAGE_SIZE);
    g_heap_end = g_heap_start;
    serial_print("[heap] OK\n");
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;

    if ((size & (HEAP_ALIGN - 1)))
        size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);

    size_t total = size + HEADER_SIZE;
    if (total < MIN_BLOCK)
        total = MIN_BLOCK;

    heap_header_t *curr = g_heap_start;
    while (curr) {
        if (curr->free && curr->size >= total) {
            if (curr->size >= total + MIN_BLOCK) {
                heap_header_t *new_h = (heap_header_t *) ((uintptr_t) curr + total);
                new_h->size = curr->size - total;
                new_h->free = 1;
                new_h->next = curr->next;
                new_h->prev = curr;
                if (new_h->next)
                    new_h->next->prev = new_h;
                curr->next = new_h;
                curr->size = total;
            }
            curr->free = 0;
            return (void *) ((uintptr_t) curr + HEADER_SIZE);
        }
        curr = curr->next;
    }

    size_t grow = total > 0x10000 ? total : 0x10000;
    size_t pages = (grow + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t virt = g_heap_virt_top;
    for (size_t i = 0; i < pages; i++) {
        void *frame = pmm_alloc();
        if (!frame) {
            serial_print("[heap] OOM!\n");
            return NULL;
        }
        paging_map_page(virt + i * PAGE_SIZE, (uint64_t) frame, PAGE_RW);
    }
    g_heap_virt_top += pages * PAGE_SIZE;

    heap_header_t *new_h = heap_create_block(virt, pages * PAGE_SIZE);
    new_h->prev = g_heap_end;
    g_heap_end->next = new_h;
    g_heap_end = new_h;

    /* tenta alocar do bloco que acabou de criar */
    if (new_h->size >= total) {
        if (new_h->size >= total + MIN_BLOCK) {
            heap_header_t *split_h = (heap_header_t *) ((uintptr_t) new_h + total);
            split_h->size = new_h->size - total;
            split_h->free = 1;
            split_h->next = new_h->next;
            split_h->prev = new_h;
            new_h->next = split_h;
            new_h->size = total;
        }
        new_h->free = 0;
        return (void *) ((uintptr_t) new_h + HEADER_SIZE);
    }

    return NULL;
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    heap_header_t *h = (heap_header_t *) ((uintptr_t) ptr - HEADER_SIZE);
    h->free = 1;

    if (h->prev && h->prev->free)
        heap_coalesce(h->prev);
    else
        heap_coalesce(h);
}
