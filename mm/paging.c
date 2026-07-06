#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include <stddef.h>

static page_table_t *g_pml4 = NULL;

/* Obtem o PML4 atual do CR3 */
static void paging_load_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_pml4 = (page_table_t *) (cr3 & PAGE_ADDR_MASK);
}

static page_table_t *paging_get_or_create_table(page_entry_t *entry) {
    if (*entry & PAGE_PRESENT) {
        return (page_table_t *) ((*entry & PAGE_ADDR_MASK));
    }
    page_table_t *table = (page_table_t *) pmm_alloc();
    if (!table)
        return NULL;
    for (int i = 0; i < PAGE_TABLE_ENTRIES; i++)
        table->entries[i] = 0;
    *entry = (uint64_t) table | PAGE_PRESENT | PAGE_RW;
    return table;
}

void paging_init(void) {
    serial_print("[paging] init\n");
    paging_load_cr3();
    serial_print("[paging] PML4 em ");
}

int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd   = (virt >> 21) & 0x1FF;
    int idx_pt   = (virt >> 12) & 0x1FF;

    uint64_t pte_flags = flags | PAGE_PRESENT;

    page_entry_t *pml4_e = &g_pml4->entries[idx_pml4];
    page_table_t *pdpt = (page_table_t *) paging_get_or_create_table(pml4_e);
    if (!pdpt) return -1;

    page_entry_t *pdpt_e = &pdpt->entries[idx_pdpt];
    page_table_t *pd = (page_table_t *) paging_get_or_create_table(pdpt_e);
    if (!pd) return -1;

    page_entry_t *pd_e = &pd->entries[idx_pd];
    page_table_t *pt = (page_table_t *) paging_get_or_create_table(pd_e);
    if (!pt) return -1;

    pt->entries[idx_pt] = (phys & PAGE_ADDR_MASK) | pte_flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int paging_unmap_page(uint64_t virt) {
    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd   = (virt >> 21) & 0x1FF;
    int idx_pt   = (virt >> 12) & 0x1FF;

    page_entry_t *pml4_e = &g_pml4->entries[idx_pml4];
    if (!(*pml4_e & PAGE_PRESENT)) return -1;

    page_table_t *pdpt = (page_table_t *) (*pml4_e & PAGE_ADDR_MASK);
    page_entry_t *pdpt_e = &pdpt->entries[idx_pdpt];
    if (!(*pdpt_e & PAGE_PRESENT)) return -1;

    page_table_t *pd = (page_table_t *) (*pdpt_e & PAGE_ADDR_MASK);
    page_entry_t *pd_e = &pd->entries[idx_pd];
    if (!(*pd_e & PAGE_PRESENT)) return -1;

    page_table_t *pt = (page_table_t *) (*pd_e & PAGE_ADDR_MASK);
    page_entry_t *pt_e = &pt->entries[idx_pt];
    if (!(*pt_e & PAGE_PRESENT)) return -1;

    pt->entries[idx_pt] = 0;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

uint64_t paging_get_phys(uint64_t virt) {
    int idx_pml4 = (virt >> 39) & 0x1FF;
    int idx_pdpt = (virt >> 30) & 0x1FF;
    int idx_pd   = (virt >> 21) & 0x1FF;
    int idx_pt   = (virt >> 12) & 0x1FF;

    page_entry_t *pml4_e = &g_pml4->entries[idx_pml4];
    if (!(*pml4_e & PAGE_PRESENT)) return 0;

    page_table_t *pdpt = (page_table_t *) (*pml4_e & PAGE_ADDR_MASK);
    page_entry_t *pdpt_e = &pdpt->entries[idx_pdpt];
    if (!(*pdpt_e & PAGE_PRESENT)) return 0;

    page_table_t *pd = (page_table_t *) (*pdpt_e & PAGE_ADDR_MASK);
    page_entry_t *pd_e = &pd->entries[idx_pd];

    if (*pd_e & PAGE_HUGE) {
        return ((*pd_e & PAGE_ADDR_MASK) | (virt & 0x1FFFFF));
    }
    if (!(*pd_e & PAGE_PRESENT)) return 0;

    page_table_t *pt = (page_table_t *) (*pd_e & PAGE_ADDR_MASK);
    page_entry_t *pt_e = &pt->entries[idx_pt];
    if (!(*pt_e & PAGE_PRESENT)) return 0;

    return ((*pt_e & PAGE_ADDR_MASK) | (virt & 0xFFF));
}

int paging_map_range(uint64_t virt, uint64_t phys, size_t pages, uint64_t flags) {
    for (size_t i = 0; i < pages; i++) {
        if (paging_map_page(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) < 0)
            return -1;
    }
    return 0;
}
