/* GCC m68k SysV C ABI -> Amiga library register ABI, 68000 only.
   Save A6 and D2 (Write) as required by BOTH ABIs. No V36 calls. */
.section .text.start,"ax"
.globl _start
_start:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l %a0,%a2
    move.l %d0,%d2
    move.l 4.w,%a6
    move.l %a6,SysBase
    suba.l %a1,%a1
    jsr -294(%a6)
    move.l %d0,%a3
    tst.l 172(%a3)
    beq wb
    move.l %d2,-(%sp)
    move.l %a2,-(%sp)
    jsr diag_main
    addq.l #8,%sp
    bra finished
wb:
    lea 92(%a3),%a0
    jsr -384(%a6)
    lea 92(%a3),%a0
    jsr -372(%a6)
    move.l %d0,%a2
    jsr -132(%a6)
    move.l %a2,%a1
    jsr -378(%a6)
    moveq #20,%d0
finished:
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.text
.globl os_open_library
os_open_library:
    move.l %a6,-(%sp)
    move.l 8(%sp),%a1
    move.l 12(%sp),%d0
    move.l SysBase,%a6
    jsr -552(%a6)
    move.l %d0,%a0 /* SysV pointer return */
    move.l (%sp)+,%a6
    rts
.globl os_close_library
os_close_library:
    move.l %a6,-(%sp)
    move.l 8(%sp),%a1
    move.l SysBase,%a6
    jsr -414(%a6)
    move.l (%sp)+,%a6
    rts
.globl os_open_resource
os_open_resource:
    move.l %a6,-(%sp)
    move.l 8(%sp),%a1
    move.l SysBase,%a6
    jsr -498(%a6)
    move.l %d0,%a0 /* SysV pointer return */
    move.l (%sp)+,%a6
    rts
.macro simple name, base, offset
.globl \name
\name:
    move.l %a6,-(%sp)
    move.l \base,%a6
    jsr \offset(%a6)
    move.l (%sp)+,%a6
    rts
.endm
simple os_disable,SysBase,-120
simple os_enable,SysBase,-126
simple os_output,DOSBase,-60
.globl os_signals
os_signals:
    move.l %a6,-(%sp)
    moveq #0,%d0
    moveq #0,%d1
    move.l SysBase,%a6
    jsr -306(%a6)
    move.l (%sp)+,%a6
    rts
.globl os_alloc_misc
os_alloc_misc:
    move.l %a6,-(%sp)
    move.l 8(%sp),%d0
    move.l 12(%sp),%a1
    move.l MiscBase,%a6
    jsr -6(%a6)
    move.l %d0,%a0 /* SysV pointer return */
    move.l (%sp)+,%a6
    rts
.globl os_free_misc
os_free_misc:
    move.l %a6,-(%sp)
    move.l 8(%sp),%d0
    move.l MiscBase,%a6
    jsr -12(%a6)
    move.l (%sp)+,%a6
    rts
.globl os_write
os_write:
    movem.l %d2-%d3/%a6,-(%sp)
    move.l 16(%sp),%d1
    move.l 20(%sp),%d2
    move.l 24(%sp),%d3
    move.l DOSBase,%a6
    jsr -48(%a6)
    movem.l (%sp)+,%d2-%d3/%a6
    rts
.section .note.GNU-stack,"",@progbits
