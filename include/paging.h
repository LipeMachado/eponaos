#ifndef EPONA_PAGING_H
#define EPONA_PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 512

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_RW       (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_PWT      (1ULL << 3)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_ACCESSED (1ULL << 5)
#define PAGE_DIRTY    (1ULL << 6)
#define PAGE_PAT      (1ULL << 7)
#define PAGE_GLOBAL   (1ULL << 8)
#define PAGE_NX       (1ULL << 63)

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* 2MiB page (huge page) */
#define PAGE_HUGE     (1ULL << 7)

typedef uint64_t page_entry_t;

typedef struct {
    page_entry_t entries[PAGE_TABLE_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

void paging_init(void);
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int paging_unmap_page(uint64_t virt);
uint64_t paging_get_phys(uint64_t virt);
int paging_map_range(uint64_t virt, uint64_t phys, size_t pages, uint64_t flags);
int paging_add_flags(uint64_t virt, uint64_t flags);
void paging_flush_tlb(void);
uint64_t paging_clone_current(void);
int paging_map_page_on(page_table_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t paging_get_phys_on(page_table_t *pml4, uint64_t virt);

#endif
