; ============================================================================
; EponaOS - boot/stage1.asm
; Stage 1 do bootloader (MBR - Master Boot Record).
;
; A BIOS carrega este setor (512 bytes) no endereco fisico 0x7C00 e executa
; em REAL MODE (16 bits). Nossa unica tarefa aqui e:
;   1. preparar segmentos e stack
;   2. carregar o Stage 2 do disco para a memoria (INT 13h, LBA)
;   3. saltar para o Stage 2
;
; Stage 1 e pequeno demais (512B) para fazer o boot inteiro, entao ele apenas
; "puxa" o Stage 2, que tem espaco para trocar de modo e carregar o kernel.
; ============================================================================

BITS 16                     ; CPU comeca em 16 bits (real mode)
ORG  0x7C00                 ; a BIOS nos coloca em 0x7C00; labels partem daqui

; ---- constantes de layout (ver plano: Layout de disco / Mapa de memoria) ----
STAGE2_LOAD_SEG   equ 0x0000    ; segmento destino do Stage 2
STAGE2_LOAD_OFF   equ 0x8000    ; offset   destino do Stage 2 -> fisico 0x8000
STAGE2_LBA        equ 1         ; Stage 2 comeca no setor 1 (logo apos o MBR)
STAGE2_SECTORS    equ 16        ; quantos setores do Stage 2 carregar (8 KiB)

start:
    cli                     ; desliga interrupcoes enquanto mexemos em segmentos
    xor ax, ax              ; AX = 0
    mov ds, ax              ; DS = 0  (dados)
    mov es, ax              ; ES = 0  (destino de string ops / disco)
    mov ss, ax              ; SS = 0  (stack)
    mov sp, 0x7C00          ; stack logo abaixo do nosso codigo, cresce p/ baixo
    mov [boot_drive], dl    ; a BIOS entrega o numero do drive de boot em DL
    sti                     ; religa interrupcoes (a BIOS precisa delas)

    mov si, msg_boot        ; imprime "EponaOS stage1..."
    call print_string

    ; ---- carrega o Stage 2 via INT 13h AH=42h (Extended Read, usa LBA) ----
    mov si, dap             ; DS:SI -> Disk Address Packet
    mov ah, 0x42            ; funcao: extended read
    mov dl, [boot_drive]    ; drive de onde ler
    int 0x13                ; chama a BIOS
    jc  disk_error          ; CF=1 => falhou

    mov si, msg_ok
    call print_string

    ; ---- salta para o Stage 2 (far jump: define CS:IP) ----
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

; ----------------------------------------------------------------------------
disk_error:
    mov si, msg_err
    call print_string
.hang:
    hlt
    jmp .hang

; ----------------------------------------------------------------------------
; print_string: imprime string ASCIIZ apontada por DS:SI usando o teletype
; da BIOS (INT 10h, AH=0x0E). Preserva os registradores.
; ----------------------------------------------------------------------------
print_string:
    pusha
.loop:
    lodsb                   ; AL = [DS:SI], SI++
    test al, al             ; AL == 0 ? (fim da string)
    jz .done
    mov ah, 0x0E            ; teletype output
    mov bh, 0x00            ; pagina 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ---- dados ------------------------------------------------------------------
boot_drive db 0

msg_boot db "EponaOS stage1...", 13, 10, 0
msg_ok   db "stage2 carregado, pulando", 13, 10, 0
msg_err  db "ERRO DE DISCO (stage1)", 13, 10, 0

; Disk Address Packet (DAP) para INT 13h AH=42h  -> 16 bytes
align 4
dap:
    db 0x10                 ; tamanho do pacote (16 bytes)
    db 0x00                 ; reservado
    dw STAGE2_SECTORS       ; numero de setores a ler
    dw STAGE2_LOAD_OFF      ; offset  do buffer destino
    dw STAGE2_LOAD_SEG      ; segmento do buffer destino
    dq STAGE2_LBA           ; LBA inicial (64 bits)

; ---- preenche ate 510 e coloca a assinatura de boot ------------------------
times 510-($-$$) db 0
dw 0xAA55                   ; assinatura obrigatoria do MBR
