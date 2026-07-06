BITS 64
default rel
section .text
extern main
global _start
_start:
    xor rbp, rbp
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call main
    mov rdi, rax
    mov rax, 0
    int 0x80
