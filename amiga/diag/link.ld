ENTRY(_start)
SECTIONS {
  . = 0;
  .image : { KEEP(*(.text.start)) *(.text*) *(.rodata*) *(.data*)
             /* Materialize zero-initialized storage in the single code hunk. */
             . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(4); BYTE(0); . = ALIGN(4); }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
