; kernel/gdt_flush.asm — carrega GDT/TSS e recarrega os segmentos (64-bit).
BITS 64
section .text
global gdt_flush
global tss_flush

; void gdt_flush(void *gdt_ptr)   ; RDI = &gdtr
gdt_flush:
    lgdt [rdi]                    ; carrega a nova GDT

    mov ax, 0x10                  ; seletor de dados do kernel
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

; recarrega CS via "far return" (nao da pra mov cs em 64-bit)
    pop rax                       ; rax = endereco de retorno
    push 0x08                     ; novo CS (kernel code)
    push rax                      ; RIP de retorno
    o64 retf                      ; carrega CS:RIP juntos -> volta pro C

; void tss_flush(uint64_t selector)  ; RDI = 0x28
tss_flush:
    mov ax, di
    ltr ax
    ret
