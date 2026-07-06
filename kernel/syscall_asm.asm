BITS 64
default rel
section .text

extern syscall_handler_c
extern g_elf_ret_rip
extern g_elf_ret_rsp
extern g_kernel_cr3

; void enter_usermode(void *func, void *stack)
; RDI = ponteiro da funcao ring 3
; RSI = topo da pilha do usuario
global enter_usermode
enter_usermode:
    push 0x23          ; SS = user data | RPL 3
    push rsi           ; RSP = user stack top
    push 0x2           ; RFLAGS = IF clear while user ELF runs outside scheduler
    push 0x1B          ; CS = user code | RPL 3
    push rdi           ; RIP = funcao
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    iretq

; int 0x80 stub — salva registradores, chama C, restaura
global int80_stub
int80_stub:
    push 0
    push 128

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call syscall_handler_c

    cmp qword [rsp + 144], 0x08
    je .return_to_kernel

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

.return_to_kernel:
    mov rax, [g_kernel_cr3]
    mov cr3, rax
    mov rsp, [g_elf_ret_rsp]
    mov rax, [g_elf_ret_rip]
    mov qword [g_elf_ret_rip], 0
    sti
    jmp rax

; Funcao de teste executada em ring 3
global user_test_ring3
user_test_ring3:
    mov rax, 1          ; SYS_WRITE
    mov rdi, 1          ; stdout
    lea rsi, [rel msg]
    mov rdx, 19         ; tamanho da mensagem
    int 0x80

    mov rax, 0          ; SYS_EXIT
    xor rdi, rdi
    int 0x80

    hlt
    jmp user_test_ring3

msg:
    db "Hello from ring 3!", 10

; ---- SYS_EXIT return helpers ----

; elf_return_thunk — restore kernel CR3, then ret back to shell
global elf_return_thunk
elf_return_thunk:
    mov rax, [g_kernel_cr3]
    mov cr3, rax
    sti
    ret

; enter_usermode_save_ret — save return address + CR3, switch to process CR3, enter usermode
; RDI = entry, RSI = stack_top, RDX = process CR3 (PML4 phys)
global enter_usermode_save_ret
enter_usermode_save_ret:
    pop rax                    ; return address to caller (shell)
    mov [g_elf_ret_rip], rax
    mov [g_elf_ret_rsp], rsp
    push rax                   ; restore stack for enter_usermode

    mov rax, cr3
    mov [g_kernel_cr3], rax
    mov cr3, rdx               ; switch to process page tables

    jmp enter_usermode         ; tail-call: RDI, RSI already set
