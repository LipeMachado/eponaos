; boot/stage2.asm — carrega o kernel do disco e salta para ele em long mode.
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64            ; default se assemblar sem -D
%endif

BITS 16
ORG  0x8000

CODE64_SEL equ 0x08
DATA_SEL   equ 0x10
KERNEL_LBA        equ 17             ; 1 (stage1) + 16 (stage2)
KERNEL_BOUNCE_SEG equ 0x1000         ; 0x1000:0000 = fisico 0x10000
KERNEL_PHYS       equ 0x100000       ; destino final: 1 MiB
KERNEL_READ_CHUNK equ 64             ; BIOSes often reject reads > 127 sectors
E820_COUNT equ 0x4000        ; onde gravamos o nº de entradas (u32)
E820_MAP   equ 0x4004        ; onde comecam as entradas (24 bytes cada)
VGA_FONT_8X16 equ 0xE000
VBE_MODE_INFO equ 0xF000
VBE_MODE equ 0x118           ; 1024x768, geralmente 24/32bpp no QEMU/Bochs VBE

stage2_start:
    cld
    mov [boot_drive], dl             ; DL veio do stage1 = drive de boot

    mov si, msg_stage2
    call print_string

    ; A20
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    ; carrega o kernel do disco -> 0x10000 em blocos seguros para a BIOS
    call load_kernel
    jc  kernel_load_error
    mov si, msg_kloaded
    call print_string
    call do_e820
    mov si, msg_e820
    call print_string
    mov dword [VBE_MODE_INFO], 0
    call setup_vbe

    ; transicao para long mode (igual ao ciclo 2)
    call setup_paging
    cli
    lgdt [gdt_descriptor]
    mov eax, cr4
    or  eax, 1 << 5
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 << 8
    wrmsr
    mov eax, cr0
    or  eax, (1 << 31) | (1 << 0)
    mov cr0, eax
    jmp CODE64_SEL:long_mode_start

kernel_load_error:
    mov si, msg_kerr
    call print_string
.h: hlt
    jmp .h

load_kernel:
    mov word [kernel_sectors_left], KERNEL_SECTORS
    mov word [kernel_load_seg], KERNEL_BOUNCE_SEG
    mov dword [kernel_lba_current], KERNEL_LBA
    mov dword [kernel_lba_current + 4], 0

.next:
    mov ax, [kernel_sectors_left]
    test ax, ax
    jz .done

    cmp ax, KERNEL_READ_CHUNK
    jbe .size_ok
    mov ax, KERNEL_READ_CHUNK
.size_ok:
    mov [kernel_dap_count], ax
    mov bx, [kernel_load_seg]
    mov [kernel_dap_seg], bx
    mov edx, [kernel_lba_current]
    mov [kernel_dap_lba], edx
    mov edx, [kernel_lba_current + 4]
    mov [kernel_dap_lba + 4], edx

    mov si, kernel_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .fail

    mov ax, [kernel_dap_count]
    sub [kernel_sectors_left], ax

    movzx ebx, ax
    add [kernel_lba_current], ebx
    adc dword [kernel_lba_current + 4], 0

    shl ax, 5                  ; sectors * 512 / 16 = segment paragraphs
    add [kernel_load_seg], ax
    jmp .next

.done:
    clc
    ret
.fail:
    stc
    ret

setup_paging:
    cld
    mov di, 0x1000
    xor ax, ax
    mov cx, 0x3000 / 2
    rep stosw
    mov di, 0x1000
    mov dword [di], 0x2000 | 0x3
    mov di, 0x2000
    mov dword [di], 0x3000 | 0x3
    mov di, 0x3000
    mov eax, 0x00000083
    mov cx, 512
.fill_pd:
    mov dword [di], eax
    mov dword [di+4], 0
    add eax, 0x200000
    add di, 8
    loop .fill_pd
    ret

setup_vbe:
    cld
    ; Copia a fonte BIOS 8x16 para memoria baixa, para o kernel desenhar texto no framebuffer.
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    push es
    push ds
    mov ax, es
    mov ds, ax
    mov si, bp
    xor ax, ax
    mov es, ax
    mov di, VGA_FONT_8X16
    mov cx, 4096
    rep movsb
    pop ds
    pop es

    ; Consulta informacoes do modo VBE e ativa linear framebuffer.
    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_INFO
    mov ax, 0x4F01
    mov cx, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov ax, 0x4F02
    mov bx, VBE_MODE | 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov si, msg_vbe
    call print_string
    ret
.fail:
    mov dword [VBE_MODE_INFO], 0
    mov si, msg_vbe_fail
    call print_string
    ret
; coleta o mapa de memoria via INT 15h/E820 -> grava em E820_MAP, count em E820_COUNT
do_e820:
    mov di, E820_MAP
    xor ebx, ebx                 ; continuation = 0 (inicio)
    xor bp, bp                   ; bp = contador de entradas
    mov edx, 0x0534D4150         ; assinatura 'SMAP'
    mov eax, 0xE820
    mov dword [es:di + 20], 1    ; forca entrada de 24 bytes (bit ACPI valido)
    mov ecx, 24
    int 0x15
    jc  .failed                  ; CF => E820 nao suportado
    mov edx, 0x0534D4150         ; alguns BIOS zeram edx
    cmp eax, edx
    jne .failed
    test ebx, ebx
    je  .failed
    jmp .jmpin
.lp:
    mov eax, 0xE820
    mov dword [es:di + 20], 1
    mov ecx, 24
    int 0x15
    jc  .done                    ; CF aqui = ja pegou a ultima
    mov edx, 0x0534D4150
.jmpin:
    jcxz .skip                   ; entrada de tamanho 0 -> pula
    cmp cl, 20                   ; entrada tem campo ACPI (24 bytes)?
    jbe .noext
    test byte [es:di + 20], 1    ; bit "ignorar esta entrada"
    je  .skip
.noext:
    mov ecx, [es:di + 8]         ; comprimento (low)
    or  ecx, [es:di + 12]        ; | comprimento (high)
    jz  .skip                    ; comprimento 0 -> pula
    inc bp
    add di, 24
.skip:
    test ebx, ebx
    jne .lp
.done:
    movzx eax, bp
    mov [E820_COUNT], eax
    clc
    ret
.failed:
    xor eax, eax
    mov [E820_COUNT], eax        ; count=0 sinaliza falha
    stc
    ret

print_string:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    int 0x10
    jmp .loop
.done:
    popa
    ret

boot_drive  db 0
msg_stage2  db "stage2: A20 + carregando kernel...", 13, 10, 0
msg_kloaded db "kernel carregado, indo p/ long mode", 13, 10, 0
msg_kerr    db "ERRO ao carregar kernel!", 13, 10, 0
msg_e820    db "mapa de memoria E820 coletado", 13, 10, 0
msg_vbe     db "VBE framebuffer ativado", 13, 10, 0
msg_vbe_fail db "VBE indisponivel, usando VGA text", 13, 10, 0

; Disk Address Packet para carregar o kernel
kernel_dap:
    db 0x10
    db 0x00
kernel_dap_count:
    dw 0
    dw 0x0000                        ; offset
kernel_dap_seg:
    dw 0                              ; segmento -> 0x10000+
kernel_dap_lba:
    dq 0

kernel_sectors_left dw 0
kernel_load_seg     dw 0
kernel_lba_current  dq 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF            ; code64 (L=1)
    dq 0x00AF92000000FFFF            ; data
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 64
long_mode_start:
    cld
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; copia o kernel do bounce buffer (0x10000) para 1 MiB (0x100000)
    mov rsi, 0x10000
    mov rdi, KERNEL_PHYS
    mov rcx, (KERNEL_SECTORS * 512) / 8   ; em qwords
    rep movsq

    mov rax, KERNEL_PHYS
    jmp rax                          ; salta para o _start do kernel

times (16*512)-($-$$) db 0
