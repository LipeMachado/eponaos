BITS 64
default rel

section .text
global _start
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [msg]
    mov rdx, 16
    int 0x80

    mov rax, 0
    xor rdi, rdi
    int 0x80

    hlt
    jmp _start

section .rodata
msg: db "Hello from ELF!", 10
