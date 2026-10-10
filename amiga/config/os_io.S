/* V34 DOS only; no SetMode/ReadArgs/CreateMsgPort (V36) dependency. */
.text
.globl cfg_os_open
cfg_os_open:
    movem.l %d2/%a6,-(%sp)
    move.l 12(%sp),%d1
    move.l 16(%sp),%d2
    move.l DOSBase,%a6
    jsr -30(%a6)
    movem.l (%sp)+,%d2/%a6
    rts
.globl cfg_os_close
cfg_os_close:
    move.l %a6,-(%sp)
    move.l 8(%sp),%d1
    move.l DOSBase,%a6
    jsr -36(%a6)
    move.l (%sp)+,%a6
    rts
.globl cfg_os_read
cfg_os_read:
    movem.l %d2-%d3/%a6,-(%sp)
    move.l 16(%sp),%d1
    move.l 20(%sp),%d2
    move.l 24(%sp),%d3
    move.l DOSBase,%a6
    jsr -42(%a6)
    movem.l (%sp)+,%d2-%d3/%a6
    rts
.globl cfg_os_delay
cfg_os_delay:
    move.l %a6,-(%sp)
    move.l 8(%sp),%d1
    move.l DOSBase,%a6
    jsr -198(%a6)
    move.l (%sp)+,%a6
    rts
.section .note.GNU-stack,"",@progbits
