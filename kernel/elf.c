#include "elf.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include <stdint.h>

#define ELF_USER_STACK_SIZE 0x5000

int elf_load(const char *path, uint64_t *entry, uint64_t *stack_top, uint64_t *pml4_out) {
    char full[64];
    full[0] = '/';
    int pl = (int) strlen(path);
    if (pl > 62) pl = 62;
    memcpy(full + 1, path, (size_t) pl);
    full[1 + pl] = 0;

    file_t *f = vfs_open(full);
    if (!f) {
        serial_print("[elf] not found: ");
        serial_print(full);
        serial_print("\n");
        return -1;
    }

    elf64_hdr_t hdr;
    int n = vfs_read(f, sizeof(hdr), &hdr);
    if (n < (int)sizeof(hdr)) {
        serial_print("[elf] header read failed\n");
        vfs_close(f);
        return -1;
    }

    uint32_t magic = *(uint32_t*)hdr.e_ident;
    if (magic != ELF_MAGIC) {
        serial_print("[elf] bad magic\n");
        vfs_close(f);
        return -1;
    }

    int phnum = hdr.e_phnum;
    if (phnum > 16) {
        serial_print("[elf] too many phdrs\n");
        vfs_close(f);
        return -1;
    }

    elf64_phdr_t phdrs[16];
    f->offset = hdr.e_phoff;
    n = vfs_read(f, phnum * sizeof(elf64_phdr_t), (void*)phdrs);
    if (n < phnum * (int)sizeof(elf64_phdr_t)) {
        serial_print("[elf] phdrs read failed\n");
        vfs_close(f);
        return -1;
    }

    uint64_t pml4_phys = paging_clone_current();
    if (!pml4_phys) return -1;
    page_table_t *proc_pml4 = (page_table_t *)pml4_phys;

    for (int i = 0; i < phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = phdrs[i].p_vaddr;
        uint64_t memsz = phdrs[i].p_memsz;
        uint64_t filesz = phdrs[i].p_filesz;

        uint64_t start_page = vaddr & ~(uint64_t)0xFFF;
        uint64_t end_page = (vaddr + memsz + 0xFFF) & ~(uint64_t)0xFFF;

        uint64_t page_flags = PAGE_USER | PAGE_RW;

        for (uint64_t page = start_page; page < end_page; page += PAGE_SIZE) {
            uint64_t phys = (uint64_t)pmm_alloc();
            if (!phys) {
                serial_print("[elf] oom\n");
                vfs_close(f);
                return -1;
            }
            memset((void*)phys, 0, PAGE_SIZE);
            if (paging_map_page_on(proc_pml4, page, phys, page_flags) < 0) {
                serial_print("[elf] map fail\n");
                vfs_close(f);
                return -1;
            }
        }

        if (filesz > 0) {
            f->offset = phdrs[i].p_offset;
            uint64_t remaining = filesz;
            uint64_t write_addr = vaddr;
            uint8_t tmp[512];

            while (remaining > 0) {
                uint64_t chunk = remaining;
                if (chunk > 512) chunk = 512;

                int nr = vfs_read(f, chunk, tmp);
                if (nr <= 0) break;

                uint64_t phys = paging_get_phys_on(proc_pml4, write_addr);
                if (phys) {
                    for (uint64_t j = 0; j < (uint64_t)nr; j++)
                        ((uint8_t*)phys)[j] = tmp[j];
                }
                write_addr += (uint64_t)nr;
                remaining -= (uint64_t)nr;
            }
        }

        for (uint64_t j = filesz; j < memsz; j++) {
            uint64_t phys = paging_get_phys_on(proc_pml4, vaddr + j);
            if (phys)
                *(uint8_t*)phys = 0;
        }
    }

    vfs_close(f);

    {
        uint64_t code_phys = paging_get_phys_on(proc_pml4, 0x400000);
        serial_print("[elf] code phys=");
        serial_print_hex(code_phys);
        serial_print(" bytes: ");
        if (code_phys) {
            uint8_t *check = (uint8_t*)code_phys;
            for (int i = 0; i < 32; i++) {
                serial_print_hex((uint64_t)check[i]);
                serial_print(" ");
            }
        }
        serial_print("\n");
    }

    uint64_t user_stack_base = 0x70000000 - ELF_USER_STACK_SIZE;
    *stack_top = 0x70000000;

    for (uint64_t off = 0; off < ELF_USER_STACK_SIZE; off += PAGE_SIZE) {
        uint64_t phys = (uint64_t)pmm_alloc();
        if (!phys) {
            serial_print("[elf] stack oom\n");
            return -1;
        }
        memset((void*)phys, 0, PAGE_SIZE);
        if (paging_map_page_on(proc_pml4, user_stack_base + off, phys, PAGE_USER | PAGE_RW) < 0) {
            serial_print("[elf] stack map fail\n");
            return -1;
        }
    }

    /* Set up initial stack: argc=0, argv=NULL */
    {
        uint64_t stack_top_phys = paging_get_phys_on(proc_pml4, 0x70000000 - 16);
        uint64_t stack_top_off  = (0x70000000 - 16) & 0xFFF;
        uint64_t *args = (uint64_t *)(stack_top_phys + stack_top_off);
        args[0] = 0;          /* argc */
        args[1] = 0;          /* argv (NULL) */
        *stack_top = (uint64_t)(0x70000000 - 16);
    }

    *entry = hdr.e_entry;
    *pml4_out = pml4_phys;
    return 0;
}
