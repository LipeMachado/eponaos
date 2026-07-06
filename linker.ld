ENTRY(_start)

PHDRS {
    text   PT_LOAD FLAGS(5); /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data   PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS {
      . = 1M;                      /* kernel carregado em 0x100000 (doc/03 §8) */

      .text : {
          *(.text.boot)           /* entry.asm vem PRIMEIRO (em 0x100000) */
          *(.text*)
      } :text

      .rodata : { *(.rodata*) } :rodata

      .data   : { *(.data*) } :data

      .bss : {
          __bss_start = .;
          *(COMMON)
          *(.bss*)
          __bss_end = .;
      } :data
   
      /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
