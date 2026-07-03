; kernel/entry.asm — primeiro código do kernel (64-bit), em 0x100000.
  BITS 64
  section .text.boot
  global _start 
  extern kernel_main
  extern __bss_start
  extern __bss_end

  _start:
      mov rsp, stack_top          ; 1) prepara a stack (C precisa dela)
  
      mov rdi, __bss_start        ; 2) zera o .bss (globais nao-inicializadas)
      mov rcx, __bss_end
      sub rcx, rdi                ; rcx = tamanho do bss em bytes
      xor al, al
      rep stosb                   ; escreve 0 em [rdi], rcx vezes
  
      call kernel_main            ; 3) entra no C

  .hang:                          ; se o C retornar, trava com seguranca
      cli
      hlt
      jmp .hang

  section .bss
  align 16
  stack_bottom: 
      resb 16384                  ; 16 KiB de stack
  stack_top: