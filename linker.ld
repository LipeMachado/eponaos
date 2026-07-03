ENTRY(_start)
  
  SECTIONS {
      . = 1M;                      /* kernel carregado em 0x100000 (doc/03 §8) */

      .text : {
          *(.text.boot)           /* entry.asm vem PRIMEIRO (em 0x100000) */
          *(.text*)
      }
      .rodata : { *(.rodata*) }
      .data   : { *(.data*) }
      .bss : {
          __bss_start = .;
          *(COMMON)
          *(.bss*)
          __bss_end = .;
      }
  
      /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
  }